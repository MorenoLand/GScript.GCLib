#include "TClient.h"
#include "TClientPacketDispatch.h"
#include "TClientPlayerProps.h"
#include "TClientProtocol.h"
#include "TClientProtocolV5.h"
#include "TClientProtocolV6.h"
#include "IEnums.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

namespace {
static bool sockets_ready() {
#ifdef _WIN32
    static bool initialized = false;
    static std::mutex m;
    std::lock_guard<std::mutex> lock(m);
    if (initialized) return true;
    WSADATA data;
    initialized = WSAStartup(MAKEWORD(2, 2), &data) == 0;
    return initialized;
#else
    return true;
#endif
}

static void close_socket(socket_t s) {
    if (s == invalid_socket_value) return;
#ifdef _WIN32
    closesocket(s);
#else
    close(s);
#endif
}

static bool send_all(socket_t s, const uint8_t* data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        int result = send(s, reinterpret_cast<const char*>(data + sent), static_cast<int>(len - sent), 0);
        if (result <= 0) return false;
        sent += static_cast<size_t>(result);
    }
    return true;
}

static bool encode_send_packet(TClient* client, int packet_id, const uint8_t* data, int length) {
    if (!client || !client->connected || client->sock == invalid_socket_value) return false;
    if (!client->protocol || !client->protocol->build_packet_frame) return false;
    std::lock_guard<std::mutex> lock(client->send_mutex);
    std::vector<uint8_t> framed;
    if (!client->protocol->build_packet_frame(client, packet_id, data, length, framed)) return false;
    if (client->version.generation == 6 && client->v6_outgoing_crypto_enabled) {
        tclient_v6_encrypt_stream(client, framed.data(), framed.size());
    }
    return send_all(client->sock, framed.data(), framed.size());
}

static bool send_login_packet(TClient* client, const char* account, const char* password) {
    if (!client || !client->connected || client->sock == invalid_socket_value) return false;
    if (!client->protocol || !client->protocol->build_login_frame) return false;
    std::lock_guard<std::mutex> lock(client->send_mutex);
    std::vector<uint8_t> framed;
    if (!client->protocol->build_login_frame(client, account, password, framed)) return false;
    if (client->version.generation == 6 && client->v6_outgoing_crypto_enabled) {
        tclient_v6_encrypt_stream(client, framed.data(), framed.size());
    }
    return send_all(client->sock, framed.data(), framed.size());
}

static std::string comma_text_item(const char* value) {
    std::string s = value ? value : "";
    bool quote = s.find_first_of(",\"\r\n") != std::string::npos;
    if (!quote) return s;
    std::string out = "\"";
    for (char c : s) {
        if (c == '"') out += "\"\"";
        else if (c != '\r' && c != '\n') out += c;
    }
    out += "\"";
    return out;
}

static std::string comma_text3(const char* a, const char* b, const char* c) {
    return comma_text_item(a) + "," + comma_text_item(b) + "," + comma_text_item(c);
}

static void set_error(TClient* client, const char* message) {
    if (!client) return;
    std::lock_guard<std::mutex> lock(client->cb_mutex);
    client->last_error = message ? message : "";
}

static char* copy_string_for_abi(const std::string& value) {
    char* out = new char[value.size() + 1];
    std::memcpy(out, value.c_str(), value.size() + 1);
    return out;
}

static std::unordered_set<std::string> parse_type_filter(const char* comma_separated_types) {
    std::unordered_set<std::string> types;
    if (!comma_separated_types) return types;
    std::stringstream ss(comma_separated_types);
    std::string item;
    while (std::getline(ss, item, ',')) {
        size_t start = item.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) continue;
        size_t end = item.find_last_not_of(" \t\r\n");
        types.insert(item.substr(start, end - start + 1));
    }
    return types;
}

static std::string json_escape_local(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    static const char* hex = "0123456789abcdef";
    for (unsigned char c : value) {
        switch (c) {
        case '\\': out += "\\\\"; break;
        case '"': out += "\\\""; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20 || c >= 0x7f) {
                out += "\\u00";
                out.push_back(hex[c >> 4]);
                out.push_back(hex[c & 0x0f]);
            } else {
                out.push_back(static_cast<char>(c));
            }
        }
    }
    return out;
}

static std::string player_state_json_for_abi(const TClientPlayerState& player) {
    std::vector<std::string> keys;
    keys.reserve(player.fields.size());
    for (const auto& kv : player.fields) keys.push_back(kv.first);
    std::sort(keys.begin(), keys.end());
    std::string json = "{\"player_id\":" + std::to_string(player.id) +
                       ",\"connected\":" + (player.connected ? "true" : "false");
    for (const auto& key : keys) {
        json += ",\"" + json_escape_local(key) + "\":" + player.fields.at(key);
    }
    json += "}";
    return json;
}

static bool read_exact(socket_t s, uint8_t* out, size_t len) {
    size_t got = 0;
    while (got < len) {
        int result = recv(s, reinterpret_cast<char*>(out + got), static_cast<int>(len - got), 0);
        if (result <= 0) return false;
        got += static_cast<size_t>(result);
    }
    return true;
}

static bool send_handshake_if_needed(TClient* gc) {
    std::string handshake = gc->handshake_override.empty()
        ? (gc->version.handshake ? gc->version.handshake : "")
        : gc->handshake_override;
    if (handshake.empty()) return true;
    return send_all(gc->sock, reinterpret_cast<const uint8_t*>(handshake.data()), handshake.size());
}

static void recv_loop(TClient* gc) {
    while (!gc->stop) {
        std::vector<uint8_t> frame;
        if (gc->version.generation == 6) {
            uint8_t header[6] = {0, 0, 0, 0, 0, 0};
            if (!read_exact(gc->sock, header, 6)) break;
            tclient_v6_decrypt_stream(gc, header, 6);
            int len = (static_cast<int>(header[2]) << 16) | (static_cast<int>(header[3]) << 8) | static_cast<int>(header[4]);
            if (len < 6 || len > 0x1000000) break;
            frame.assign(header, header + 6);
            frame.resize(static_cast<size_t>(len));
            if (!read_exact(gc->sock, frame.data() + 6, frame.size() - 6)) break;
            tclient_v6_decrypt_stream(gc, frame.data() + 6, frame.size() - 6);
        } else {
            uint8_t header[2] = {0, 0};
            if (!read_exact(gc->sock, header, 2)) break;
            int len = (static_cast<int>(header[0]) << 8) | static_cast<int>(header[1]);
            if (len == 0xffff) {
                uint8_t ext[4] = {0, 0, 0, 0};
                if (!read_exact(gc->sock, ext, 4)) break;
                len = (static_cast<int>(ext[0]) << 24) |
                      (static_cast<int>(ext[1]) << 16) |
                      (static_cast<int>(ext[2]) << 8) |
                      static_cast<int>(ext[3]);
            }
            if (len <= 0 || len > 0x1000000) break;
            frame.resize(static_cast<size_t>(len));
            if (!read_exact(gc->sock, frame.data(), frame.size())) break;
        }
        if (gc->protocol && gc->protocol->decode_packets) {
            std::vector<TClientDecodedPacket> packets;
            if (gc->protocol->decode_packets(gc, frame, packets)) {
                for (const auto& packet : packets) {
                    tclient_dispatch_packet(gc, packet.packet_id, packet.payload);
                }
            }
        } else if (gc->protocol && gc->protocol->decode_frame) {
            std::vector<uint8_t> decrypted;
            if (gc->protocol->decode_frame(gc, frame, decrypted)) {
                tclient_process_decrypted(gc, decrypted);
            }
        }
    }

    tclient_flush_pending_level_capture(gc, 0);
    bool was_connected = gc->connected.exchange(false);
    if (was_connected) {
        CallbackDisconnected cb;
        {
            std::lock_guard<std::mutex> lock(gc->cb_mutex);
            cb = gc->on_disconnected;
        }
        if (cb.cb) cb.cb(gc->stop ? "Disconnected" : "Socket closed", cb.ud);
    }
}

static TClient* as_client(GCHandle handle) {
    return reinterpret_cast<TClient*>(handle);
}

} // namespace

extern "C" {

GCLIB_API GCHandle gc_create(const char* host, int port, const char* version) {
    auto* gc = new TClient();
    gc->host = host ? host : "";
    gc->port = port;
    gc->version = tclient_resolve_gen5_version(version);
    gc->protocol = tclient_protocol_ops(gc->version);
    std::random_device rd;
    gc->enc_key = static_cast<int>(rd() & 0x7f);
    if (gc->enc_key == 0) gc->enc_key = 1;
    return gc;
}

GCLIB_API GCHandle gc_create_version(const char* host, int port, GCVersion version) {
    switch (version) {
    case GC_VERSION_222:
        return gc_create(host, port, "2.22");
    case GC_VERSION_6037_LINUX:
        return gc_create(host, port, "6.037_linux");
    case GC_VERSION_62:
        return gc_create(host, port, "6.2");
    case GC_VERSION_6037:
    default:
        return gc_create(host, port, "6.037");
    }
}

GCLIB_API void gc_destroy(GCHandle handle) {
    auto* gc = as_client(handle);
    if (!gc) return;
    gc_disconnect(handle);
    delete gc;
}

GCLIB_API void gc_set_client_info(GCHandle handle, const char* client_info) {
    auto* gc = as_client(handle);
    if (!gc) return;
    std::lock_guard<std::mutex> lock(gc->send_mutex);
    gc->client_info_override = client_info ? client_info : "";
}

GCLIB_API void gc_set_client_identity(GCHandle handle, const char* platform, const char* id1, const char* id2, const char* id3, const char* id4) {
    auto* gc = as_client(handle);
    if (!gc) return;
    std::lock_guard<std::mutex> lock(gc->send_mutex);
    gc->client_info_override.clear();
    gc->client_platform_override = platform ? platform : "";
    gc->client_id1 = id1 ? id1 : "";
    gc->client_id2 = id2 ? id2 : "";
    gc->client_id3 = id3 ? id3 : "";
    gc->client_id4 = id4 ? id4 : "";
}

GCLIB_API void gc_set_handshake(GCHandle handle, const char* handshake) {
    auto* gc = as_client(handle);
    if (!gc) return;
    std::lock_guard<std::mutex> lock(gc->send_mutex);
    gc->handshake_override = handshake ? handshake : "";
}

GCLIB_API void gc_set_encryption_certificate_pem(GCHandle handle, const char* certificate_pem) {
    auto* gc = as_client(handle);
    if (!gc) return;
    std::lock_guard<std::mutex> lock(gc->send_mutex);
    gc->encryption_certificate_pem = certificate_pem ? certificate_pem : "";
}

GCLIB_API int gc_set_encryption_certificate_file(GCHandle handle, const char* path) {
    auto* gc = as_client(handle);
    if (!gc || !path || !*path) return 0;
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        set_error(gc, "Could not open encryption certificate file");
        return 0;
    }
    std::string pem((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    std::lock_guard<std::mutex> lock(gc->send_mutex);
    gc->encryption_certificate_pem = pem;
    return 1;
}

GCLIB_API int gc_connect(GCHandle handle) {
    auto* gc = as_client(handle);
    if (!gc) return 0;
    if (!gc->protocol) {
        set_error(gc, "Selected protocol generation is not implemented yet");
        return 0;
    }
    if (gc->host.empty() || gc->port <= 0) {
        set_error(gc, "Host or port is empty");
        return 0;
    }
    if (!sockets_ready()) {
        set_error(gc, "Socket startup failed");
        return 0;
    }

    struct addrinfo hints {};
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family = AF_UNSPEC;
    std::string port = std::to_string(gc->port);
    struct addrinfo* results = nullptr;
    if (getaddrinfo(gc->host.c_str(), port.c_str(), &hints, &results) != 0) {
        set_error(gc, "Could not resolve host");
        return 0;
    }

    socket_t s = invalid_socket_value;
    for (auto* ai = results; ai; ai = ai->ai_next) {
        s = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (s == invalid_socket_value) continue;
        if (::connect(s, ai->ai_addr, static_cast<int>(ai->ai_addrlen)) == 0) break;
        close_socket(s);
        s = invalid_socket_value;
    }
    freeaddrinfo(results);
    if (s == invalid_socket_value) {
        set_error(gc, "Could not connect to host");
        return 0;
    }

    gc->sock = s;
    gc->stop = false;
    gc->connected = true;
    gc->authenticated = false;
    gc->first_packet = true;
    gc->first_packet_out = true;
    gc->in_iter = 0x04A80B38;
    gc->out_iter = 0x04A80B38;
    gc->packet_index = 0;
    gc->v6_incoming_crypto_enabled = false;
    gc->v6_outgoing_crypto_enabled = false;
    gc->v6_rc4_i = 0;
    gc->v6_rc4_j = 0;
    gc->v6_rc4_out_i = 0;
    gc->v6_rc4_out_j = 0;
    set_error(gc, "");
    if (!send_handshake_if_needed(gc)) {
        close_socket(gc->sock);
        gc->sock = invalid_socket_value;
        gc->connected = false;
        set_error(gc, "Could not send protocol handshake");
        return 0;
    }
    gc->recv_thread = std::thread(recv_loop, gc);

    CallbackConnected cb;
    {
        std::lock_guard<std::mutex> lock(gc->cb_mutex);
        cb = gc->on_connected;
    }
    if (cb.cb) cb.cb(cb.ud);
    return 1;
}

GCLIB_API int gc_login(GCHandle handle, const char* account, const char* password) {
    auto* gc = as_client(handle);
    if (!gc || !gc->connected) return 0;
    if (!gc->protocol || !gc->protocol->build_login_frame) {
        set_error(gc, "Selected protocol generation is not implemented yet");
        return 0;
    }
    if (!send_login_packet(gc, account ? account : "", password ? password : "")) {
        set_error(gc, "Could not send login packet");
        return 0;
    }
    return 1;
}

GCLIB_API void gc_disconnect(GCHandle handle) {
    auto* gc = as_client(handle);
    if (!gc) return;
    gc->stop = true;
    if (gc->sock != invalid_socket_value) {
#ifdef _WIN32
        shutdown(gc->sock, SD_BOTH);
#else
        shutdown(gc->sock, SHUT_RDWR);
#endif
        close_socket(gc->sock);
        gc->sock = invalid_socket_value;
    }
    if (gc->recv_thread.joinable()) gc->recv_thread.join();
    tclient_flush_pending_level_capture(gc, 0);
    gc->connected = false;
    gc->authenticated = false;
}

GCLIB_API int gc_is_connected(GCHandle handle) {
    auto* gc = as_client(handle);
    return gc && gc->connected ? 1 : 0;
}

GCLIB_API int gc_is_authenticated(GCHandle handle) {
    auto* gc = as_client(handle);
    return gc && gc->authenticated ? 1 : 0;
}

GCLIB_API int gc_poll(GCHandle handle) {
    return gc_is_connected(handle);
}

GCLIB_API const char* gc_get_last_error(GCHandle handle) {
    auto* gc = as_client(handle);
    if (!gc) return "Invalid handle";
    return gc->last_error.c_str();
}

GCLIB_API char* gc_get_player_json(GCHandle handle, int player_id) {
    auto* gc = as_client(handle);
    if (!gc) return copy_string_for_abi("{}");
    std::lock_guard<std::mutex> lock(gc->players_mutex);
    auto it = gc->players.find(player_id);
    if (it == gc->players.end()) return copy_string_for_abi("{}");
    return copy_string_for_abi(player_state_json_for_abi(it->second));
}

GCLIB_API char* gc_get_self_player_json(GCHandle handle) {
    auto* gc = as_client(handle);
    if (!gc) return copy_string_for_abi("{}");
    std::lock_guard<std::mutex> lock(gc->players_mutex);
    if (gc->self_player_id < 0) return copy_string_for_abi("{}");
    auto it = gc->players.find(gc->self_player_id);
    if (it == gc->players.end()) return copy_string_for_abi("{}");
    return copy_string_for_abi(player_state_json_for_abi(it->second));
}

GCLIB_API int gc_set_resource_dump_directory(GCHandle handle, const char* directory) {
    auto* gc = as_client(handle);
    if (!gc) return 0;
    std::lock_guard<std::mutex> lock(gc->cb_mutex);
    gc->resource_dump_directory = directory ? directory : "";
    return 1;
}

GCLIB_API int gc_set_resource_dump_types(GCHandle handle, const char* comma_separated_types) {
    auto* gc = as_client(handle);
    if (!gc) return 0;
    std::lock_guard<std::mutex> lock(gc->cb_mutex);
    gc->resource_dump_types = parse_type_filter(comma_separated_types);
    return 1;
}

#define SET_CB(name, field, type) \
GCLIB_API void name(GCHandle handle, type callback, void* user_data) { \
    auto* gc = as_client(handle); \
    if (!gc) return; \
    std::lock_guard<std::mutex> lock(gc->cb_mutex); \
    gc->field.cb = callback; \
    gc->field.ud = user_data; \
}

SET_CB(gc_on_connected, on_connected, GC_OnConnected)
SET_CB(gc_on_disconnected, on_disconnected, GC_OnDisconnected)
SET_CB(gc_on_authenticated, on_authenticated, GC_OnAuthenticated)
SET_CB(gc_on_raw_packet, on_raw_packet, GC_OnRawPacket)
SET_CB(gc_on_packet_event, on_packet_event, GC_OnPacketEvent)
SET_CB(gc_on_chat, on_chat, GC_OnChat)
SET_CB(gc_on_chat_ex, on_chat_ex, GC_OnChatEx)
SET_CB(gc_on_private_message, on_private_message, GC_OnPrivateMessage)
SET_CB(gc_on_level_name, on_level_name, GC_OnLevelName)
SET_CB(gc_on_player_warp, on_player_warp, GC_OnPlayerWarp)
SET_CB(gc_on_player_warp2, on_player_warp2, GC_OnPlayerWarp2)
SET_CB(gc_on_player_props, on_player_props, GC_OnPlayerProps)
SET_CB(gc_on_other_player_props, on_other_player_props, GC_OnOtherPlayerProps)
SET_CB(gc_on_player_left, on_player_left, GC_OnPlayerLeft)
SET_CB(gc_on_board_packet, on_board_packet, GC_OnBoardPacket)
SET_CB(gc_on_file, on_file, GC_OnFile)
SET_CB(gc_on_file_failed, on_file_failed, GC_OnFileFailed)
SET_CB(gc_on_world_time, on_world_time, GC_OnWorldTime)
SET_CB(gc_on_npc_props, on_npc_props, GC_OnNpcProps)
SET_CB(gc_on_npc_deleted, on_npc_deleted, GC_OnNpcDeleted)
SET_CB(gc_on_sign, on_sign, GC_OnSign)
SET_CB(gc_on_explosion, on_explosion, GC_OnExplosion)
SET_CB(gc_on_hit_objects, on_hit_objects, GC_OnHitObjects)
SET_CB(gc_on_server_text, on_server_text, GC_OnServerText)
SET_CB(gc_on_flag_set, on_flag_set, GC_OnFlagSet)
SET_CB(gc_on_flag_del, on_flag_del, GC_OnFlagDel)
SET_CB(gc_on_weapon_script, on_weapon_script, GC_OnWeaponScript)
SET_CB(gc_on_resource, on_resource, GC_OnResource)

#undef SET_CB

GCLIB_API int gc_send_packet(GCHandle handle, int packet_id, const void* data, int length) {
    auto* gc = as_client(handle);
    const auto* bytes = reinterpret_cast<const uint8_t*>(data);
    return encode_send_packet(gc, packet_id, bytes, length) ? 1 : 0;
}

GCLIB_API int gc_send_chat(GCHandle handle, const char* message) {
    return gc_send_level_chat(handle, message);
}

GCLIB_API int gc_send_level_chat(GCHandle handle, const char* message) {
    std::vector<uint8_t> data;
    write_gchar(data, TC_PLPROP_CURCHAT);
    write_gstring(data, message ? message : "");
    return gc_send_packet(handle, PLI_PLAYERPROPS, data.data(), static_cast<int>(data.size()));
}

GCLIB_API int gc_send_toall_chat(GCHandle handle, const char* message) {
    std::string s = message ? message : "";
    return gc_send_packet(handle, PLI_TOALL, s.data(), static_cast<int>(s.size()));
}

GCLIB_API int gc_set_player_prop_string(GCHandle handle, int prop_id, const char* value) {
    std::vector<uint8_t> data;
    write_gchar(data, prop_id);
    write_gstring(data, value ? value : "");
    return gc_send_packet(handle, PLI_PLAYERPROPS, data.data(), static_cast<int>(data.size()));
}

GCLIB_API int gc_set_player_prop_byte(GCHandle handle, int prop_id, int value) {
    std::vector<uint8_t> data;
    write_gchar(data, prop_id);
    write_gchar(data, value);
    return gc_send_packet(handle, PLI_PLAYERPROPS, data.data(), static_cast<int>(data.size()));
}

GCLIB_API int gc_set_nickname(GCHandle handle, const char* nickname) {
    return gc_set_player_prop_string(handle, TC_PLPROP_NICKNAME, nickname);
}

GCLIB_API int gc_set_head_image(GCHandle handle, const char* image) {
    return gc_set_player_prop_string(handle, TC_PLPROP_HEADIMAGE, image);
}

GCLIB_API int gc_set_body_image(GCHandle handle, const char* image) {
    return gc_set_player_prop_string(handle, TC_PLPROP_BODYIMAGE, image);
}

GCLIB_API int gc_set_player_attribute(GCHandle handle, int attribute_index, const char* value) {
    int prop = 0;
    if (attribute_index >= 1 && attribute_index <= 5) {
        prop = TC_PLPROP_GATTRIB1 + (attribute_index - 1);
    } else if (attribute_index >= 6 && attribute_index <= 9) {
        prop = TC_PLPROP_GATTRIB6 + (attribute_index - 6);
    } else if (attribute_index >= 10 && attribute_index <= 30) {
        prop = TC_PLPROP_GATTRIB10 + (attribute_index - 10);
    } else {
        auto* gc = as_client(handle);
        set_error(gc, "Player attribute index must be between 1 and 30");
        return 0;
    }
    return gc_set_player_prop_string(handle, prop, value);
}

GCLIB_API int gc_set_player_status(GCHandle handle, int status) {
    return gc_set_player_prop_byte(handle, TC_PLPROP_STATUS, status);
}

GCLIB_API int gc_set_player_colors(GCHandle handle, int skin, int coat, int sleeves, int shoes, int belt) {
    std::vector<uint8_t> data;
    write_gchar(data, TC_PLPROP_COLORS);
    write_gchar(data, skin);
    write_gchar(data, coat);
    write_gchar(data, sleeves);
    write_gchar(data, shoes);
    write_gchar(data, belt);
    return gc_send_packet(handle, PLI_PLAYERPROPS, data.data(), static_cast<int>(data.size()));
}

GCLIB_API int gc_move(GCHandle handle, float x, float y, int direction, int use_high_precision) {
    std::vector<uint8_t> data;
    write_gchar(data, TC_PLPROP_ATTACHNPC);
    write_gchar(data, 0);
    write_gint3(data, 0);
    write_gchar(data, TC_PLPROP_DIRECTION);
    write_gchar(data, direction);
    if (use_high_precision) {
        write_gchar(data, TC_PLPROP_X2);
        write_position2(data, x);
        write_gchar(data, TC_PLPROP_Y2);
        write_position2(data, y);
    } else {
        write_gchar(data, TC_PLPROP_X);
        write_gchar(data, std::max(0, std::min(223, static_cast<int>(x * 2.0f))));
        write_gchar(data, TC_PLPROP_Y);
        write_gchar(data, std::max(0, std::min(223, static_cast<int>(y * 2.0f))));
    }
    return gc_send_packet(handle, PLI_PLAYERPROPS, data.data(), static_cast<int>(data.size()));
}

GCLIB_API int gc_walk_step(GCHandle handle, float x, float y, int direction, const char* gani, int use_high_precision) {
    std::vector<uint8_t> data;
    write_gchar(data, TC_PLPROP_ATTACHNPC);
    write_gchar(data, 0);
    write_gint3(data, 0);
    write_gchar(data, TC_PLPROP_DIRECTION);
    write_gchar(data, direction);
    if (gani && *gani) {
        write_gchar(data, TC_PLPROP_GANI);
        write_gstring(data, gani);
    }
    if (use_high_precision) {
        write_gchar(data, TC_PLPROP_X2);
        write_position2(data, x);
        write_gchar(data, TC_PLPROP_Y2);
        write_position2(data, y);
    } else {
        write_gchar(data, TC_PLPROP_X);
        write_gchar(data, std::max(0, std::min(223, static_cast<int>(x * 2.0f))));
        write_gchar(data, TC_PLPROP_Y);
        write_gchar(data, std::max(0, std::min(223, static_cast<int>(y * 2.0f))));
    }
    return gc_send_packet(handle, PLI_PLAYERPROPS, data.data(), static_cast<int>(data.size()));
}

GCLIB_API int gc_level_warp(GCHandle handle, float x, float y, const char* level) {
    std::vector<uint8_t> data;
    write_gchar(data, static_cast<int>(x * 2.0f));
    write_gchar(data, static_cast<int>(y * 2.0f));
    std::string s = level ? level : "";
    data.insert(data.end(), s.begin(), s.end());
    return gc_send_packet(handle, PLI_LEVELWARP, data.data(), static_cast<int>(data.size()));
}

GCLIB_API int gc_set_animation(GCHandle handle, const char* gani, float x, float y, int direction) {
    std::vector<uint8_t> data;
    write_gchar(data, TC_PLPROP_GANI);
    write_gstring(data, gani ? gani : "");
    write_gchar(data, TC_PLPROP_X);
    write_gchar(data, static_cast<int>(x * 2.0f));
    write_gchar(data, TC_PLPROP_Y);
    write_gchar(data, static_cast<int>(y * 2.0f));
    write_gchar(data, TC_PLPROP_DIRECTION);
    write_gchar(data, direction);
    return gc_send_packet(handle, PLI_PLAYERPROPS, data.data(), static_cast<int>(data.size()));
}

GCLIB_API int gc_request_file(GCHandle handle, const char* filename) {
    std::string s = filename ? filename : "";
    return gc_send_packet(handle, PLI_WANTFILE, s.data(), static_cast<int>(s.size()));
}

GCLIB_API int gc_request_level_board(GCHandle handle, const char* level, int mod_time, int x, int y, int width, int height) {
    auto* gc = as_client(handle);
    if (!gc) return 0;
    std::string level_name = level ? level : "";
    if (level_name.empty()) {
        std::lock_guard<std::mutex> lock(gc->players_mutex);
        level_name = gc->current_level;
    }
    if (level_name.empty()) {
        set_error(gc, "No current level is known");
        return 0;
    }
    if (level_name.size() > 223) level_name.resize(223);
    if (width <= 0) width = 64;
    if (height <= 0) height = 64;
    std::vector<uint8_t> data;
    write_gchar(data, static_cast<int>(level_name.size()));
    data.insert(data.end(), level_name.begin(), level_name.end());
    write_gint5(data, mod_time);
    write_gshort(data, x);
    write_gshort(data, y);
    write_gshort(data, width);
    write_gshort(data, height);
    return encode_send_packet(gc, PLI_REQUESTUPDATEBOARD, data.data(), static_cast<int>(data.size())) ? 1 : 0;
}

GCLIB_API int gc_send_private_message(GCHandle handle, const int* player_ids, int count, const char* message) {
    if (!player_ids || count < 0) return 0;
    std::vector<uint8_t> data;
    write_gshort(data, count);
    for (int i = 0; i < count; ++i) write_gshort(data, player_ids[i]);
    std::string s = message ? message : "";
    data.insert(data.end(), s.begin(), s.end());
    return gc_send_packet(handle, PLI_PRIVATEMESSAGE, data.data(), static_cast<int>(data.size()));
}

GCLIB_API int gc_set_flag(GCHandle handle, const char* flag) {
    std::string s = flag ? flag : "";
    return gc_send_packet(handle, PLI_FLAGSET, s.data(), static_cast<int>(s.size()));
}

GCLIB_API int gc_unset_flag(GCHandle handle, const char* flag) {
    std::string s = flag ? flag : "";
    return gc_send_packet(handle, PLI_FLAGDEL, s.data(), static_cast<int>(s.size()));
}

GCLIB_API int gc_request_text(GCHandle handle, const char* type, const char* key, const char* default_value) {
    std::string payload = comma_text3(type, key, default_value);
    return gc_send_packet(handle, PLI_REQUESTTEXT, payload.data(), static_cast<int>(payload.size()));
}

GCLIB_API int gc_send_text(GCHandle handle, const char* type, const char* key, const char* metadata, const char* value) {
    std::string payload = comma_text3(type, key, metadata);
    payload += "\n";
    payload += value ? value : "";
    return gc_send_packet(handle, PLI_SENDTEXT, payload.data(), static_cast<int>(payload.size()));
}

GCLIB_API int gc_request_weapon_script(GCHandle handle, const char* name) {
    std::string s = name ? name : "";
    return gc_send_packet(handle, PLI_UPDATESCRIPT, s.data(), static_cast<int>(s.size()));
}

GCLIB_API int gc_request_class_script(GCHandle handle, const char* name, int mod_time) {
    std::vector<uint8_t> data;
    write_gint5(data, mod_time);
    std::string s = name ? name : "";
    data.insert(data.end(), s.begin(), s.end());
    return gc_send_packet(handle, PLI_UPDATECLASS, data.data(), static_cast<int>(data.size()));
}

GCLIB_API int gc_request_gani_script(GCHandle handle, const char* name, int mod_time) {
    std::vector<uint8_t> data;
    write_gint5(data, mod_time);
    std::string s = name ? name : "";
    data.insert(data.end(), s.begin(), s.end());
    return gc_send_packet(handle, PLI_UPDATEGANI, data.data(), static_cast<int>(data.size()));
}

GCLIB_API int gc_send_trigger_action_npc(GCHandle handle, int npc_id, float x, float y, const char* action, const char* params) {
    std::vector<uint8_t> data;
    if (npc_id <= 0) {
        data.push_back(' ');
        data.push_back(' ');
        data.push_back(' ');
    } else {
        write_gint3(data, npc_id);
    }
    int enc_x = std::max(0, std::min(220, static_cast<int>(std::floor(x * 2.0f + 0.5f))));
    int enc_y = std::max(0, std::min(220, static_cast<int>(std::floor(y * 2.0f + 0.5f))));
    data.push_back(static_cast<uint8_t>(enc_x + 32));
    data.push_back(static_cast<uint8_t>(enc_y + 32));
    if (action) data.insert(data.end(), action, action + std::strlen(action));
    if (params && *params) {
        data.push_back(',');
        data.insert(data.end(), params, params + std::strlen(params));
    }
    return gc_send_packet(handle, PLI_TRIGGERACTION, data.data(), static_cast<int>(data.size()));
}

GCLIB_API int gc_send_trigger_action(GCHandle handle, float x, float y, const char* action, const char* params) {
    return gc_send_trigger_action_npc(handle, 0, x, y, action, params);
}

GCLIB_API int gc_send_hit_objects(GCHandle handle, float x, float y, int power, int npc_id) {
    std::vector<uint8_t> data;
    int enc_power = std::max(0, std::min(127, power));
    int enc_x = std::max(0, std::min(127, static_cast<int>(std::floor(x * 2.0f + 0.5f))));
    int enc_y = std::max(0, std::min(127, static_cast<int>(std::floor(y * 2.0f + 0.5f))));
    data.push_back(static_cast<uint8_t>(enc_power + 32));
    data.push_back(static_cast<uint8_t>(enc_x + 32));
    data.push_back(static_cast<uint8_t>(enc_y + 32));
    if (npc_id > 0) write_gint3(data, npc_id);
    return gc_send_packet(handle, PLI_HITOBJECTS, data.data(), static_cast<int>(data.size()));
}

GCLIB_API int gc_send_player_hurt(GCHandle handle, int target_player_id, float from_x, float from_y, float target_x, float target_y, int power) {
    std::vector<uint8_t> data;
    write_gint3(data, target_player_id);
    double dx = (target_x + 1.5 - from_x) * 0.5;
    double dy = (target_y + 2.0 - from_y) * 0.5;
    double len = std::hypot(dx, dy);
    if (len > 0.0) { dx /= len; dy /= len; }
    auto hurt_coord = [](double v) -> uint8_t {
        if (v < -4.0) return 0x20;
        if (v > 4.0) return 0xa0;
        int enc = static_cast<int>(std::floor((v + 4.0) * 16.0 + 0.0001));
        return static_cast<uint8_t>(std::max(0, std::min(128, enc)) + 0x20);
    };
    data.push_back(hurt_coord(dx));
    data.push_back(hurt_coord(dy));
    data.push_back(static_cast<uint8_t>(std::max(0, std::min(127, power)) + 32));
    return gc_send_packet(handle, PLI_HURTPLAYER, data.data(), static_cast<int>(data.size()));
}

GCLIB_API int gc_send_add_bomb(GCHandle handle, float x, float y, int power, int fuse_ticks, const char* image) {
    std::vector<uint8_t> data;
    int enc_x = std::max(0, std::min(127, static_cast<int>(std::floor(x * 2.0f + 0.5f))));
    int enc_y = std::max(0, std::min(127, static_cast<int>(std::floor(y * 2.0f + 0.5f))));
    data.push_back(static_cast<uint8_t>(enc_x + 32));
    data.push_back(static_cast<uint8_t>(enc_y + 32));
    data.push_back(static_cast<uint8_t>(((power & 7) | 8) + 32));
    data.push_back(static_cast<uint8_t>(std::max(0, std::min(127, fuse_ticks)) + 32));
    if (image && *image) {
        std::string img = image;
        if (img.size() > 223) img.resize(223);
        data.insert(data.end(), img.begin(), img.end());
    }
    return gc_send_packet(handle, PLI_BOMBADD, data.data(), static_cast<int>(data.size()));
}

GCLIB_API int gc_send_del_bomb(GCHandle handle, float x, float y) {
    std::vector<uint8_t> data;
    int enc_x = std::max(0, std::min(127, static_cast<int>(std::floor(x * 2.0f + 0.5f))));
    int enc_y = std::max(0, std::min(127, static_cast<int>(std::floor(y * 2.0f + 0.5f))));
    data.push_back(static_cast<uint8_t>(enc_x + 32));
    data.push_back(static_cast<uint8_t>(enc_y + 32));
    return gc_send_packet(handle, PLI_BOMBDEL, data.data(), static_cast<int>(data.size()));
}

GCLIB_API int gc_send_item_add(GCHandle handle, float x, float y, int item_id) {
    std::vector<uint8_t> data;
    int enc_x = std::max(0, std::min(127, static_cast<int>(std::floor(x * 2.0f + 0.5f))));
    int enc_y = std::max(0, std::min(127, static_cast<int>(std::floor(y * 2.0f + 0.5f))));
    data.push_back(static_cast<uint8_t>(enc_x + 32));
    data.push_back(static_cast<uint8_t>(enc_y + 32));
    data.push_back(static_cast<uint8_t>((item_id & 0x7f) + 32));
    return gc_send_packet(handle, PLI_ITEMADD, data.data(), static_cast<int>(data.size()));
}

GCLIB_API int gc_send_item_take(GCHandle handle, float x, float y, int item_id) {
    std::vector<uint8_t> data;
    int enc_x = std::max(0, std::min(127, static_cast<int>(std::floor(x * 2.0f + 0.5f))));
    int enc_y = std::max(0, std::min(127, static_cast<int>(std::floor(y * 2.0f + 0.5f))));
    data.push_back(static_cast<uint8_t>(enc_x + 32));
    data.push_back(static_cast<uint8_t>(enc_y + 32));
    data.push_back(static_cast<uint8_t>((item_id & 0x7f) + 32));
    return gc_send_packet(handle, PLI_ITEMTAKE, data.data(), static_cast<int>(data.size()));
}

GCLIB_API int gc_send_item_del(GCHandle handle, float x, float y) {
    std::vector<uint8_t> data;
    int enc_x = std::max(0, std::min(127, static_cast<int>(std::floor(x * 2.0f + 0.5f))));
    int enc_y = std::max(0, std::min(127, static_cast<int>(std::floor(y * 2.0f + 0.5f))));
    data.push_back(static_cast<uint8_t>(enc_x + 32));
    data.push_back(static_cast<uint8_t>(enc_y + 32));
    return gc_send_packet(handle, PLI_ITEMDEL, data.data(), static_cast<int>(data.size()));
}

GCLIB_API int gc_send_open_chest(GCHandle handle, int x, int y) {
    std::vector<uint8_t> data;
    data.push_back(static_cast<uint8_t>((x & 0x7f) + 32));
    data.push_back(static_cast<uint8_t>((y & 0x7f) + 32));
    return gc_send_packet(handle, PLI_OPENCHEST, data.data(), static_cast<int>(data.size()));
}

GCLIB_API int gc_send_shot(GCHandle handle, float x, float y, int direction, int sprite, int power, int mirrored, int from_player) {
    std::vector<uint8_t> data;
    int enc_x = std::max(0, std::min(127, static_cast<int>(std::floor(x * 2.0f + 0.5f))));
    int enc_y = std::max(0, std::min(127, static_cast<int>(std::floor(y * 2.0f + 0.5f))));
    int flags = direction & 3;
    if (mirrored) flags |= 4;
    if (from_player) flags |= 8;
    data.push_back(static_cast<uint8_t>(enc_x + 32));
    data.push_back(static_cast<uint8_t>(enc_y + 32));
    data.push_back(static_cast<uint8_t>((flags & 0x7f) + 32));
    data.push_back(static_cast<uint8_t>((sprite & 0x7f) + 32));
    data.push_back(static_cast<uint8_t>((power & 0x7f) + 33));
    return gc_send_packet(handle, PLI_SHOOT, data.data(), static_cast<int>(data.size()));
}

GCLIB_API int gc_send_verify_file_crc(GCHandle handle, const char* filename, unsigned int crc32) {
    std::vector<uint8_t> data;
    write_gint5(data, static_cast<int>(crc32));
    std::string s = filename ? filename : "";
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    data.insert(data.end(), s.begin(), s.end());
    return gc_send_packet(handle, PLI_VERIFYWANTSEND, data.data(), static_cast<int>(data.size()));
}

GCLIB_API int gc_send_level_warp_modtime(GCHandle handle, float x, float y, const char* level, unsigned int mod_time) {
    std::vector<uint8_t> data;
    write_gint5(data, static_cast<int>(mod_time));
    int enc_x = std::max(0, std::min(127, static_cast<int>(std::floor(x * 2.0f + 0.5f))));
    int enc_y = std::max(0, std::min(127, static_cast<int>(std::floor(y * 2.0f + 0.5f))));
    data.push_back(static_cast<uint8_t>(enc_x + 32));
    data.push_back(static_cast<uint8_t>(enc_y + 32));
    std::string s = level ? level : "";
    data.insert(data.end(), s.begin(), s.end());
    return gc_send_packet(handle, PLI_LEVELWARPMOD, data.data(), static_cast<int>(data.size()));
}

GCLIB_API int gc_send_adjacent_level(GCHandle handle, const char* level) {
    std::string s = level ? level : "";
    return gc_send_packet(handle, PLI_ADJACENTLEVEL, s.data(), static_cast<int>(s.size()));
}

GCLIB_API int gc_send_delete_npc(GCHandle handle, int npc_id) {
    std::vector<uint8_t> data;
    write_gint3(data, npc_id);
    return gc_send_packet(handle, PLI_NPCDEL, data.data(), static_cast<int>(data.size()));
}

GCLIB_API int gc_send_put_npc(GCHandle handle, float x, float y, const char* image, const char* script) {
    std::vector<uint8_t> data;
    write_gchar(data, std::max(0, std::min(220, static_cast<int>(std::floor(x * 2.0f + 0.5f)))));
    write_gchar(data, std::max(0, std::min(220, static_cast<int>(std::floor(y * 2.0f + 0.5f)))));
    std::string img = image ? image : "";
    data.insert(data.end(), img.begin(), img.end());
    data.push_back('\n');
    std::string sc = script ? script : "";
    data.insert(data.end(), sc.begin(), sc.end());
    return gc_send_packet(handle, PLI_PUTNPC, data.data(), static_cast<int>(data.size()));
}

GCLIB_API int gc_send_rc_chat(GCHandle handle, const char* message) {
    std::string s = message ? message : "";
    return gc_send_packet(handle, PLI_RC_CHAT, s.data(), static_cast<int>(s.size()));
}

GCLIB_API int gc_send_server_warp(GCHandle handle, const char* destination) {
    std::string s = destination ? destination : "";
    return gc_send_packet(handle, PLI_SERVERWARP, s.data(), static_cast<int>(s.size()));
}

GCLIB_API int gc_send_board_modify(GCHandle handle, int layer, int x, int y, int width, int height, const unsigned short* tiles) {
    if (!tiles || width <= 0 || height <= 0) return 0;
    std::vector<uint8_t> data;
    if (layer > 0) data.push_back(static_cast<uint8_t>(layer + 0x60));
    data.push_back(static_cast<uint8_t>((x & 0xff) + 0x20));
    data.push_back(static_cast<uint8_t>((y & 0xff) + 0x20));
    data.push_back(static_cast<uint8_t>((width & 0xff) + 0x20));
    data.push_back(static_cast<uint8_t>((height & 0xff) + 0x20));
    for (int row = 0; row < height; ++row)
        for (int col = 0; col < width; ++col) {
            unsigned short t = tiles[static_cast<size_t>(row * width + col)];
            data.push_back(static_cast<uint8_t>((t >> 7) + 0x20));
            data.push_back(static_cast<uint8_t>((t & 0x7f) + 0x20));
        }
    return gc_send_packet(handle, PLI_BOARDMODIFY, data.data(), static_cast<int>(data.size()));
}

GCLIB_API int gc_send_fire_spy(GCHandle handle, int x, int y) {
    uint8_t byte = static_cast<uint8_t>(0x20 + (y & 7) + (x & 7) * 8);
    return gc_send_packet(handle, PLI_FIRESPY, &byte, 1);
}

GCLIB_API int gc_send_carry_throw(GCHandle handle) {
    return gc_send_packet(handle, PLI_THROWCARRIED, nullptr, 0);
}

GCLIB_API int gc_send_enter_level(GCHandle handle) {
    uint8_t bang = '!';
    return gc_send_packet(handle, PLI_UPDATEFILE, &bang, 1);
}

GCLIB_API int gc_send_delete_weapon(GCHandle handle, const char* name) {
    std::string s = name ? name : "";
    return gc_send_packet(handle, PLI_NPCWEAPONDEL, s.data(), static_cast<int>(s.size()));
}

GCLIB_API int gc_send_explosion(GCHandle handle, float x, float y, int power, int bomb_index) {
    // 4 bytes: x*2+0x20, y*2+0x20, bomb_index+0x20, power+0x20
    std::vector<uint8_t> data;
    int enc_x = std::max(0, std::min(127, static_cast<int>(std::floor(x * 2.0f + 0.5f))));
    int enc_y = std::max(0, std::min(127, static_cast<int>(std::floor(y * 2.0f + 0.5f))));
    data.push_back(static_cast<uint8_t>(enc_x + 0x20));
    data.push_back(static_cast<uint8_t>(enc_y + 0x20));
    data.push_back(static_cast<uint8_t>((bomb_index & 0x7f) + 0x20));
    data.push_back(static_cast<uint8_t>((power & 0x7f) + 0x20));
    return gc_send_packet(handle, PLI_EXPLOSION, data.data(), static_cast<int>(data.size()));
}

GCLIB_API int gc_send_have_window(GCHandle handle, int have, const char* name) {
    // byte: (have ? '!' : ' ') then window name
    std::vector<uint8_t> data;
    data.push_back(static_cast<uint8_t>(have ? '!' : ' '));
    std::string s = name ? name : "";
    data.insert(data.end(), s.begin(), s.end());
    return gc_send_packet(handle, PLI_WEAPONADD, data.data(), static_cast<int>(data.size()));
}

GCLIB_API int gc_send_ping_answer(GCHandle handle, int ping_value) {
    // '!' + 2-byte GraalInt: high=(val>>7)+0x20, low=(val&0x7f)+0x20
    int clamped = std::max(0, std::min(0x6fff, ping_value));
    std::vector<uint8_t> data;
    data.push_back('!');
    data.push_back(static_cast<uint8_t>((clamped >> 7) + 0x20));
    data.push_back(static_cast<uint8_t>((clamped & 0x7f) + 0x20));
    return gc_send_packet(handle, PLI_PACKETCOUNT, data.data(), static_cast<int>(data.size()));
}

GCLIB_API int gc_send_is_pker(GCHandle handle, int player_id) {
    std::vector<uint8_t> data;
    write_gint3(data, player_id);
    return gc_send_packet(handle, PLI_CLAIMPKER, data.data(), static_cast<int>(data.size()));
}

// Length-prefix encode a filename: if len <= 0xdf, byte (len+0x20) + data; else 0xff + first 0xdf bytes.
static void write_length_string(std::vector<uint8_t>& out, const std::string& s) {
    if (s.size() <= 0xdf) {
        out.push_back(static_cast<uint8_t>(s.size() + 0x20));
        out.insert(out.end(), s.begin(), s.end());
    } else {
        out.push_back(0xff);
        out.insert(out.end(), s.begin(), s.begin() + 0xdf);
    }
}

GCLIB_API int gc_send_upload_start(GCHandle handle, const char* filename) {
    std::string s = filename ? filename : "";
    return gc_send_packet(handle, PLI_RC_LARGEFILESTART, s.data(), static_cast<int>(s.size()));
}

GCLIB_API int gc_send_save_file(GCHandle handle, const char* filename, const void* data, int length) {
    // length-prefixed name + raw file data chunk
    std::string name = filename ? filename : "";
    const auto* bytes = reinterpret_cast<const uint8_t*>(data);
    std::vector<uint8_t> buf;
    write_length_string(buf, name);
    if (bytes && length > 0) buf.insert(buf.end(), bytes, bytes + length);
    return gc_send_packet(handle, PLI_RC_FILEBROWSER_UP, buf.data(), static_cast<int>(buf.size()));
}

GCLIB_API int gc_send_upload_end(GCHandle handle, const char* filename) {
    std::string s = filename ? filename : "";
    return gc_send_packet(handle, PLI_RC_LARGEFILEEND, s.data(), static_cast<int>(s.size()));
}

GCLIB_API int gc_send_request_file_deletion(GCHandle handle, const char* filename) {
    // base filename only (no directory)
    std::string s = filename ? filename : "";
    auto pos = s.find_last_of("/\\");
    if (pos != std::string::npos) s = s.substr(pos + 1);
    return gc_send_packet(handle, PLI_RC_FILEBROWSER_DELETE, s.data(), static_cast<int>(s.size()));
}

GCLIB_API int gc_send_request_folder_deletion(GCHandle handle, const char* path) {
    std::string s = path ? path : "";
    return gc_send_packet(handle, PLI_RC_FOLDERDELETE, s.data(), static_cast<int>(s.size()));
}

GCLIB_API int gc_send_request_file_rename(GCHandle handle, const char* source, const char* destination) {
    // length-prefixed destination + length-prefixed source (basename only for each)
    auto basename = [](const std::string& p) -> std::string {
        auto pos = p.find_last_of("/\\");
        return pos != std::string::npos ? p.substr(pos + 1) : p;
    };
    std::string src = basename(source ? source : "");
    std::string dst = basename(destination ? destination : "");
    std::vector<uint8_t> buf;
    write_length_string(buf, dst);
    write_length_string(buf, src);
    return gc_send_packet(handle, PLI_RC_FILEBROWSER_RENAME, buf.data(), static_cast<int>(buf.size()));
}

GCLIB_API int gc_send_request_files_move(GCHandle handle, const char* source_pattern, const char* destination_folder) {
    // length-prefixed source_pattern + raw destination_folder
    std::string src = source_pattern ? source_pattern : "";
    std::string dst = destination_folder ? destination_folder : "";
    std::vector<uint8_t> buf;
    write_length_string(buf, src);
    buf.insert(buf.end(), dst.begin(), dst.end());
    return gc_send_packet(handle, PLI_RC_FILEBROWSER_MOVE, buf.data(), static_cast<int>(buf.size()));
}

GCLIB_API int gc_send_weapon_img(GCHandle handle, const char* image) {
    std::string s = image ? image : "";
    return gc_send_packet(handle, PLI_UNKNOWN25, s.data(), static_cast<int>(s.size()));
}

GCLIB_API int gc_send_npc_props(GCHandle handle, const char* props) {
    std::string s = props ? props : "";
    return gc_send_packet(handle, PLI_NPCPROPS, s.data(), static_cast<int>(s.size()));
}

GCLIB_API int gc_send_window_list(GCHandle handle, const char* window_list) {
    std::string s = window_list ? window_list : "";
    return gc_send_packet(handle, PLI_PROCESSLIST, s.data(), static_cast<int>(s.size()));
}

GCLIB_API int gc_send_preload_level(GCHandle handle, const char* level, unsigned int mod_time) {
    std::vector<uint8_t> data;
    write_gint5(data, static_cast<int>(mod_time));
    std::string s = level ? level : "";
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    data.insert(data.end(), s.begin(), s.end());
    return gc_send_packet(handle, PLI_MUTEPLAYER, data.data(), static_cast<int>(data.size()));
}

GCLIB_API int gc_send_board_modify_rect(GCHandle handle, const char* level, int layer, int x, int y, int width, int height, const unsigned short* tiles) {
    if (!tiles || width <= 0 || height <= 0) return 0;
    std::vector<uint8_t> data;
    write_length_string(data, level ? level : "");
    if (layer > 0) data.push_back(static_cast<uint8_t>(layer + 0x60));
    data.push_back(static_cast<uint8_t>((x & 0xff) + 0x20));
    data.push_back(static_cast<uint8_t>((y & 0xff) + 0x20));
    data.push_back(static_cast<uint8_t>((width & 0xff) + 0x20));
    data.push_back(static_cast<uint8_t>((height & 0xff) + 0x20));
    for (int row = 0; row < height; ++row) {
        for (int col = 0; col < width; ++col) {
            unsigned short t = tiles[static_cast<size_t>(row * width + col)];
            data.push_back(static_cast<uint8_t>((t >> 7) + 0x20));
            data.push_back(static_cast<uint8_t>((t & 0x7f) + 0x20));
        }
    }
    return gc_send_packet(handle, 42, data.data(), static_cast<int>(data.size()));
}

GCLIB_API int gc_send_shoot_projectile(GCHandle handle, double x, double y, double z, double angle, double z_angle, double power, double gravity, const char* gani, const char* gani_data, const char* options) {
    std::vector<uint8_t> data;
    const int32_t map_x = static_cast<int32_t>(std::floor(x * (1.0 / 64.0) + 0.0001));
    const int32_t map_y = static_cast<int32_t>(std::floor(y * (1.0 / 64.0) + 0.0001));
    const int32_t enc_z = static_cast<int32_t>(std::floor(z * 64.0 + 0.0001));
    const int32_t enc_y = static_cast<int32_t>(std::floor((y - static_cast<double>(map_y << 6)) * 64.0 + 0.0001));
    const int32_t enc_x = static_cast<int32_t>(std::floor((x - static_cast<double>(map_x << 6)) * 64.0 + 0.0001));
    auto append_signed14 = [](std::vector<uint8_t>& out, int val) {
        int clamped = std::max(0, std::min(0x6fff, val));
        out.push_back(static_cast<uint8_t>((clamped >> 7) + 0x20));
        out.push_back(static_cast<uint8_t>((clamped & 0x7f) + 0x20));
    };
    append_signed14(data, enc_x);
    append_signed14(data, enc_y);
    append_signed14(data, enc_z);
    data.push_back(static_cast<uint8_t>(map_x + 0x20));
    data.push_back(static_cast<uint8_t>(map_y + 0x20));
    data.push_back(static_cast<uint8_t>(static_cast<int>(std::floor((64.0 * angle) / 6.283185307179586 + 0.0001)) + 0x20));
    data.push_back(static_cast<uint8_t>(static_cast<int>(std::floor(((z_angle + 1.5707963267948966) * 64.0) / 3.141592653589793 + 0.0001)) + 0x20));
    data.push_back(static_cast<uint8_t>(static_cast<int>(std::floor((64.0 * power) / 100.0 + 0.0001)) + 0x20));
    append_signed14(data, static_cast<int>(std::floor(64.0 * gravity * 64.0 + 0.0001)));
    std::string full_gani = gani ? gani : "";
    if (gani_data && *gani_data) { full_gani += ","; full_gani += gani_data; }
    write_length_string(data, options ? options : "");
    write_length_string(data, full_gani);
    return gc_send_packet(handle, PLI_SHOOT2, data.data(), static_cast<int>(data.size()));
}

GCLIB_API int gc_send_download_file(GCHandle handle, const char* url, const char* filename, const char* destination_path) {
    (void)handle;
    (void)url;
    (void)filename;
    (void)destination_path;
    return 1;
}

GCLIB_API int gc_send_request_update_package(GCHandle handle, const char* package_name, int blocking) {
    std::string s = package_name ? package_name : "";
    std::vector<uint8_t> data;
    data.push_back(static_cast<uint8_t>(blocking ? 1 : 0));
    data.insert(data.end(), s.begin(), s.end());
    return gc_send_packet(handle, PLI_UPDATEPACKAGEREQUESTFILE, data.data(), static_cast<int>(data.size()));
}

GCLIB_API int gc_set_encryption_out(GCHandle handle, const char* cipher_type, const char* key, const char* iv) {
    auto* gc = as_client(handle);
    if (!gc) return 0;
    (void)iv;
    std::string type = cipher_type ? cipher_type : "";
    std::transform(type.begin(), type.end(), type.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::lock_guard<std::mutex> lock(gc->send_mutex);
    if (type == "rc4" && key && *key) {
        std::vector<uint8_t> key_bytes(key, key + std::strlen(key));
        for (int i = 0; i < 256; ++i) gc->v6_rc4_out_s[i] = static_cast<uint8_t>(i);
        gc->v6_rc4_out_i = 0;
        gc->v6_rc4_out_j = 0;
        uint8_t j = 0;
        for (int i = 0; i < 256; ++i) {
            j = static_cast<uint8_t>(j + gc->v6_rc4_out_s[i] + key_bytes[static_cast<size_t>(i) % key_bytes.size()]);
            std::swap(gc->v6_rc4_out_s[i], gc->v6_rc4_out_s[j]);
        }
        gc->v6_outgoing_crypto_enabled = true;
        return 1;
    }
    gc->v6_outgoing_crypto_enabled = false;
    return 1;
}

GCLIB_API void gc_free_string(char* value) {
    delete[] value;
}

} // extern "C"

