#include "TClientProtocolV6.h"

#include "bzlib.h"
#include "miniz.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <wincrypt.h>
#include <ncrypt.h>
#endif

namespace {

enum TClientV6Compression : uint8_t {
    V6_COMP_NONE = 0,
    V6_COMP_ZLIB = 1,
    V6_COMP_BZ2 = 2
};

enum TClientV6Packet : int {
    V6_PLO_DISCONNECT = 0x10,
    V6_PLO_SIGNATURE = 0x19,
    V6_PLO_NPCWEAPONSCRIPT = 0x8c,
    V6_PLO_SET_ENCRYPTION_KEY = 0xfc,
    V6_PLO_BUNDLE = 0xfd
};

static bool zlib_decompress(const std::vector<uint8_t>& input, std::vector<uint8_t>& output) {
    mz_ulong size = std::max<mz_ulong>(static_cast<mz_ulong>(input.size() * 4), 4096);
    for (int i = 0; i < 8; ++i) {
        output.resize(size);
        mz_ulong actual = size;
        int result = uncompress(output.data(), &actual, input.data(), static_cast<mz_ulong>(input.size()));
        if (result == Z_OK) {
            output.resize(actual);
            return true;
        }
        if (result != Z_BUF_ERROR) return false;
        size *= 2;
    }
    return false;
}

static bool bz2_decompress(const std::vector<uint8_t>& input, std::vector<uint8_t>& output) {
    unsigned int size = static_cast<unsigned int>(std::max<size_t>(input.size() * 6, 8192));
    for (int i = 0; i < 10; ++i) {
        output.resize(size);
        unsigned int actual = size;
        int result = BZ2_bzBuffToBuffDecompress(reinterpret_cast<char*>(output.data()), &actual,
            const_cast<char*>(reinterpret_cast<const char*>(input.data())), static_cast<unsigned int>(input.size()), 0, 0);
        if (result == BZ_OK) {
            output.resize(actual);
            return true;
        }
        if (result != BZ_OUTBUFF_FULL) return false;
        size *= 2;
    }
    return false;
}

static bool decompress_payload(int type, const std::vector<uint8_t>& input, std::vector<uint8_t>& output) {
    if (type == V6_COMP_NONE) {
        output = input;
        return true;
    }
    if (type == V6_COMP_ZLIB) return zlib_decompress(input, output);
    if (type == V6_COMP_BZ2) return bz2_decompress(input, output);
    return false;
}

static void rc4_init(TClient* client, const std::vector<uint8_t>& key) {
    for (int i = 0; i < 256; ++i) client->v6_rc4_s[i] = static_cast<uint8_t>(i);
    client->v6_rc4_i = 0;
    client->v6_rc4_j = 0;
    uint8_t j = 0;
    for (int i = 0; i < 256; ++i) {
        j = static_cast<uint8_t>(j + client->v6_rc4_s[i] + key[static_cast<size_t>(i) % key.size()]);
        std::swap(client->v6_rc4_s[i], client->v6_rc4_s[j]);
    }
    client->v6_incoming_crypto_enabled = true;
}

#ifdef _WIN32
static std::string pem_body(const std::string& pem) {
    std::string body = pem;
    const std::string begin = "-----BEGIN PRIVATE KEY-----";
    const std::string end = "-----END PRIVATE KEY-----";
    size_t b = body.find(begin);
    if (b != std::string::npos) body.erase(b, begin.size());
    size_t e = body.find(end);
    if (e != std::string::npos) body.erase(e, end.size());
    body.erase(std::remove_if(body.begin(), body.end(), [](unsigned char c) {
        return c == '\r' || c == '\n' || c == '\t' || c == ' ';
    }), body.end());
    return body;
}

static bool rsa_private_decrypt_pkcs8(TClient* client, const std::vector<uint8_t>& encrypted, std::vector<uint8_t>& decrypted) {
    if (client->encryption_certificate_pem.empty()) {
        client->last_error = "V6 encryption key packet received but no private key was configured";
        return false;
    }

    std::string body = pem_body(client->encryption_certificate_pem);
    DWORD der_len = 0;
    if (!CryptStringToBinaryA(body.c_str(), static_cast<DWORD>(body.size()), CRYPT_STRING_BASE64, nullptr, &der_len, nullptr, nullptr)) {
        client->last_error = "Could not decode private key PEM";
        return false;
    }
    std::vector<BYTE> der(der_len);
    if (!CryptStringToBinaryA(body.c_str(), static_cast<DWORD>(body.size()), CRYPT_STRING_BASE64, der.data(), &der_len, nullptr, nullptr)) {
        client->last_error = "Could not decode private key PEM";
        return false;
    }

    NCRYPT_PROV_HANDLE prov = 0;
    NCRYPT_KEY_HANDLE key = 0;
    bool ok = false;
    SECURITY_STATUS status = NCryptOpenStorageProvider(&prov, MS_KEY_STORAGE_PROVIDER, 0);
    if (status == ERROR_SUCCESS) {
        status = NCryptImportKey(prov, 0, NCRYPT_PKCS8_PRIVATE_KEY_BLOB, nullptr, &key,
            der.data(), der_len, 0);
    }
    if (status == ERROR_SUCCESS) {
        DWORD out_len = 0;
        status = NCryptDecrypt(key, const_cast<PBYTE>(encrypted.data()), static_cast<DWORD>(encrypted.size()),
            nullptr, nullptr, 0, &out_len, NCRYPT_PAD_PKCS1_FLAG);
        if (status == ERROR_SUCCESS && out_len > 0) {
            decrypted.resize(out_len);
            status = NCryptDecrypt(key, const_cast<PBYTE>(encrypted.data()), static_cast<DWORD>(encrypted.size()),
                nullptr, decrypted.data(), out_len, &out_len, NCRYPT_PAD_PKCS1_FLAG);
        }
        if (status == ERROR_SUCCESS) {
            decrypted.resize(out_len);
            ok = true;
        } else {
            client->last_error = "Could not RSA-decrypt V6 encryption key packet";
        }
    } else {
        client->last_error = "Could not import PKCS8 RSA private key";
    }

    if (key) NCryptFreeObject(key);
    if (prov) NCryptFreeObject(prov);
    return ok;
}
#else
static bool rsa_private_decrypt_pkcs8(TClient* client, const std::vector<uint8_t>&, std::vector<uint8_t>&) {
    client->last_error = "V6 RSA private-key decrypt is not implemented on this platform yet";
    return false;
}
#endif

static bool set_incoming_encryption_key(TClient* client, const std::vector<uint8_t>& encrypted_payload) {
    std::vector<uint8_t> decrypted;
    if (!rsa_private_decrypt_pkcs8(client, encrypted_payload, decrypted)) return false;
    if (decrypted.size() < 3) {
        client->last_error = "V6 encryption key packet was too short";
        return false;
    }

    size_t pos = 0;
    int cipher_type = static_cast<int>(decrypted[pos++]) - 32;
    int key_len = static_cast<int>(decrypted[pos++]) - 32;
    if (key_len <= 0 || pos + static_cast<size_t>(key_len) > decrypted.size()) {
        client->last_error = "V6 encryption key packet had an invalid RC4 key length";
        return false;
    }
    std::vector<uint8_t> key(decrypted.begin() + static_cast<std::ptrdiff_t>(pos),
        decrypted.begin() + static_cast<std::ptrdiff_t>(pos + static_cast<size_t>(key_len)));
    pos += static_cast<size_t>(key_len);

    if (pos < decrypted.size()) {
        int iv_len = static_cast<int>(decrypted[pos++]) - 32;
        if (iv_len < 0 || pos + static_cast<size_t>(iv_len) > decrypted.size()) {
            client->last_error = "V6 encryption key packet had an invalid IV length";
            return false;
        }
    }

    if (cipher_type != 1) {
        client->last_error = "V6 server requested AES or unknown cipher; only RC4 is implemented";
        return false;
    }
    rc4_init(client, key);
    client->last_error.clear();
    return true;
}

static bool parse_header(const std::vector<uint8_t>& frame, size_t offset, int& compression, int& index, int& length, int& packet_id) {
    if (offset + 6 > frame.size()) return false;
    compression = frame[offset];
    index = frame[offset + 1];
    length = (static_cast<int>(frame[offset + 2]) << 16) |
             (static_cast<int>(frame[offset + 3]) << 8) |
             static_cast<int>(frame[offset + 4]);
    packet_id = frame[offset + 5];
    (void)index;
    if (length < 6 || offset + static_cast<size_t>(length) > frame.size()) return false;
    return true;
}

static bool decode_one(TClient* client, const std::vector<uint8_t>& frame, size_t offset, std::vector<TClientDecodedPacket>& packets) {
    int compression = 0;
    int index = 0;
    int length = 0;
    int packet_id = 0;
    if (!parse_header(frame, offset, compression, index, length, packet_id)) return false;

    std::vector<uint8_t> compressed(frame.begin() + static_cast<std::ptrdiff_t>(offset + 6),
        frame.begin() + static_cast<std::ptrdiff_t>(offset + static_cast<size_t>(length)));
    std::vector<uint8_t> payload;
    if (!decompress_payload(compression, compressed, payload)) return false;

    if (packet_id == V6_PLO_BUNDLE) {
        size_t bundle_pos = 0;
        while (bundle_pos < payload.size()) {
            int b_comp = 0;
            int b_index = 0;
            int b_len = 0;
            int b_id = 0;
            if (!parse_header(payload, bundle_pos, b_comp, b_index, b_len, b_id)) return false;
            if (!decode_one(client, payload, bundle_pos, packets)) return false;
            bundle_pos += static_cast<size_t>(b_len);
        }
        return true;
    }

    if (packet_id == V6_PLO_SET_ENCRYPTION_KEY) {
        return set_incoming_encryption_key(client, payload);
    }

    packets.push_back({packet_id, payload});
    return true;
}

static void build_v6_header(std::vector<uint8_t>& out, int compression, int packet_id, int packet_length, uint8_t packet_index) {
    out.push_back(static_cast<uint8_t>(compression & 0xff));
    out.push_back(packet_index);
    out.push_back(static_cast<uint8_t>((packet_length >> 16) & 0xff));
    out.push_back(static_cast<uint8_t>((packet_length >> 8) & 0xff));
    out.push_back(static_cast<uint8_t>(packet_length & 0xff));
    out.push_back(static_cast<uint8_t>(packet_id & 0xff));
}

} // namespace

bool tclient_v6_build_login_frame(TClient* client, const char* account, const char* password, std::vector<uint8_t>& framed) {
    std::vector<uint8_t> raw;
    raw.insert(raw.end(), client->version.protocol, client->version.protocol + std::strlen(client->version.protocol));
    write_gstring(raw, account);
    write_gstring(raw, password);
    std::string info = tclient_default_client_info(*client);
    raw.insert(raw.end(), info.begin(), info.end());
    return tclient_v6_build_packet_frame(client, 0x05, raw.data(), static_cast<int>(raw.size()), framed);
}

bool tclient_v6_build_packet_frame(TClient* client, int packet_id, const uint8_t* data, int length, std::vector<uint8_t>& framed) {
    framed.clear();
    int packet_length = length + 6;
    build_v6_header(framed, V6_COMP_NONE, packet_id, packet_length, client->packet_index++);
    if (data && length > 0) framed.insert(framed.end(), data, data + length);
    return true;
}

bool tclient_v6_decode_frame(TClient* client, const std::vector<uint8_t>& frame, std::vector<uint8_t>& decrypted) {
    decrypted.clear();
    std::vector<TClientDecodedPacket> packets;
    if (!tclient_v6_decode_packets(client, frame, packets)) return false;
    for (const auto& packet : packets) {
        decrypted.push_back(static_cast<uint8_t>((packet.packet_id + 32) & 0xff));
        decrypted.insert(decrypted.end(), packet.payload.begin(), packet.payload.end());
        decrypted.push_back('\n');
    }
    return true;
}

bool tclient_v6_decode_packets(TClient* client, const std::vector<uint8_t>& frame, std::vector<TClientDecodedPacket>& packets) {
    packets.clear();
    return decode_one(client, frame, 0, packets);
}

void tclient_v6_decrypt_stream(TClient* client, uint8_t* data, size_t length) {
    if (!client || !client->v6_incoming_crypto_enabled || !data || length == 0) return;
    for (size_t n = 0; n < length; ++n) {
        client->v6_rc4_i = static_cast<uint8_t>(client->v6_rc4_i + 1);
        client->v6_rc4_j = static_cast<uint8_t>(client->v6_rc4_j + client->v6_rc4_s[client->v6_rc4_i]);
        std::swap(client->v6_rc4_s[client->v6_rc4_i], client->v6_rc4_s[client->v6_rc4_j]);
        uint8_t k = client->v6_rc4_s[static_cast<uint8_t>(client->v6_rc4_s[client->v6_rc4_i] + client->v6_rc4_s[client->v6_rc4_j])];
        data[n] ^= k;
    }
}
