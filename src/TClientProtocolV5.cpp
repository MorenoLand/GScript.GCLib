#include "TClientProtocolV5.h"

#include "bzlib.h"
#include "miniz.h"

#include <algorithm>
#include <cstring>
#include <string>

namespace {

enum TClientCompressionType : uint8_t {
    COMP_UNCOMPRESSED = 0x02,
    COMP_ZLIB = 0x04,
    COMP_BZ2 = 0x06
};

static uint32_t advance_iterator(uint32_t iterator, int key) {
    return iterator * 0x08088405u + static_cast<uint32_t>(key);
}

static std::vector<uint8_t> crypt_bytes(const std::vector<uint8_t>& input, uint32_t& iterator, int key, int limit_blocks) {
    std::vector<uint8_t> result = input;
    int bytes_to_crypt = static_cast<int>(result.size());
    if (limit_blocks == 0) return result;
    if (limit_blocks > 0) bytes_to_crypt = std::min(bytes_to_crypt, limit_blocks * 4);

    int blocks_left = limit_blocks;
    uint32_t stream = iterator;
    for (int i = 0; i < bytes_to_crypt; ++i) {
        if ((i & 3) == 0) {
            if (blocks_left == 0) break;
            stream = advance_iterator(stream, key);
            iterator = stream;
            if (blocks_left > 0) --blocks_left;
        }
        result[static_cast<size_t>(i)] ^= static_cast<uint8_t>((stream >> ((i & 3) * 8)) & 0xff);
    }
    return result;
}

static int crypt_limit(int compression_type) {
    if (compression_type == COMP_UNCOMPRESSED) return 12;
    if (compression_type == COMP_ZLIB || compression_type == COMP_BZ2) return 4;
    return -1;
}

static bool zlib_compress(const std::vector<uint8_t>& input, std::vector<uint8_t>& output) {
    mz_ulong bound = compressBound(static_cast<mz_ulong>(input.size()));
    output.resize(bound);
    int result = compress2(output.data(), &bound, input.data(), static_cast<mz_ulong>(input.size()), Z_DEFAULT_COMPRESSION);
    if (result != Z_OK) return false;
    output.resize(bound);
    return true;
}

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

static bool bz2_compress(const std::vector<uint8_t>& input, std::vector<uint8_t>& output) {
    unsigned int out_len = static_cast<unsigned int>(input.size() + (input.size() / 100) + 601);
    output.resize(out_len);
    int result = BZ2_bzBuffToBuffCompress(reinterpret_cast<char*>(output.data()), &out_len,
        const_cast<char*>(reinterpret_cast<const char*>(input.data())), static_cast<unsigned int>(input.size()), 9, 0, 30);
    if (result != BZ_OK) return false;
    output.resize(out_len);
    return true;
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

static bool compress_payload(const std::vector<uint8_t>& input, int& type, std::vector<uint8_t>& output) {
    if (input.size() <= 55) {
        type = COMP_UNCOMPRESSED;
        output = input;
        return true;
    }
    if (input.size() > 0x2000) {
        type = COMP_BZ2;
        return bz2_compress(input, output);
    }
    type = COMP_ZLIB;
    return zlib_compress(input, output);
}

static bool decompress_payload(int type, const std::vector<uint8_t>& input, std::vector<uint8_t>& output) {
    if (type == COMP_UNCOMPRESSED) {
        output = input;
        return true;
    }
    if (type == COMP_ZLIB) return zlib_decompress(input, output);
    if (type == COMP_BZ2) return bz2_decompress(input, output);
    return false;
}

} // namespace

TClientVersionConfig tclient_resolve_gen5_version(const char* version) {
    std::string v = version ? version : "6.037";
    if (v == "2.22" || v == "222") return {"2.22", "GNW03014", "356", 5, 5, true, "win", nullptr};
    if (v == "6.037_linux" || v == "linux") return {"6.037_linux", "G3D0511C", nullptr, 5, 5, false, "linux", nullptr};
    if (v == "6.2" || v == "62" || v == "v6") return {"6.2", "G3D2504D", nullptr, 5, 6, false, "win", "GNP1905C"};
#ifdef _WIN32
    const char* platform = "win";
#elif defined(__APPLE__)
    const char* platform = "mac";
#else
    const char* platform = "linux";
#endif
    return {"6.037", "G3D0311C", nullptr, 5, 5, false, platform, nullptr};
}

bool tclient_gen5_build_login_frame(TClient* client, const char* account, const char* password, std::vector<uint8_t>& framed) {
    std::vector<uint8_t> raw;
    raw.push_back(static_cast<uint8_t>(client->version.client_type + 32));
    raw.push_back(static_cast<uint8_t>(client->enc_key + 32));
    raw.insert(raw.end(), client->version.protocol, client->version.protocol + std::strlen(client->version.protocol));
    write_gstring(raw, account);
    write_gstring(raw, password);
    if (client->version.sends_build && client->version.build) write_gstring(raw, client->version.build);
    std::string info = tclient_default_client_info(*client);
    raw.insert(raw.end(), info.begin(), info.end());

    std::vector<uint8_t> compressed;
    if (!zlib_compress(raw, compressed)) return false;
    uint16_t len = static_cast<uint16_t>(compressed.size());
    framed.clear();
    framed.push_back(static_cast<uint8_t>((len >> 8) & 0xff));
    framed.push_back(static_cast<uint8_t>(len & 0xff));
    framed.insert(framed.end(), compressed.begin(), compressed.end());
    return true;
}

bool tclient_gen5_build_packet_frame(TClient* client, int packet_id, const uint8_t* data, int length, std::vector<uint8_t>& framed) {
    std::vector<uint8_t> raw;
    raw.reserve(static_cast<size_t>(length) + 2);
    raw.push_back(static_cast<uint8_t>((packet_id + 32) & 0xff));
    if (data && length > 0) raw.insert(raw.end(), data, data + length);
    raw.push_back('\n');

    int comp_type = 0;
    std::vector<uint8_t> compressed;
    if (!compress_payload(raw, comp_type, compressed)) return false;

    std::vector<uint8_t> encrypted = crypt_bytes(compressed, client->out_iter, client->enc_key, crypt_limit(comp_type));
    uint16_t packet_len = static_cast<uint16_t>(encrypted.size() + 1);
    framed.clear();
    framed.push_back(static_cast<uint8_t>((packet_len >> 8) & 0xff));
    framed.push_back(static_cast<uint8_t>(packet_len & 0xff));
    framed.push_back(static_cast<uint8_t>(comp_type));
    framed.insert(framed.end(), encrypted.begin(), encrypted.end());
    return true;
}

bool tclient_gen5_decode_frame(TClient* client, const std::vector<uint8_t>& frame, std::vector<uint8_t>& decrypted_out) {
    if (frame.empty()) return false;
    if (client->first_packet.exchange(false) || frame[0] == 0x78) {
        return zlib_decompress(frame, decrypted_out);
    }
    int comp_type = frame[0];
    std::vector<uint8_t> encrypted(frame.begin() + 1, frame.end());
    std::vector<uint8_t> decrypted = crypt_bytes(encrypted, client->in_iter, client->enc_key, crypt_limit(comp_type));
    return decompress_payload(comp_type, decrypted, decrypted_out);
}
