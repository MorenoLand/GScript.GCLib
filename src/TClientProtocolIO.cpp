#include "TClientProtocolIO.h"

#include "TClient.h"

#include <cmath>
#include <string>

void write_gchar(std::vector<uint8_t>& out, int value) {
    out.push_back(static_cast<uint8_t>((value + 32) & 0xff));
}

void write_gshort(std::vector<uint8_t>& out, int value) {
    out.push_back(static_cast<uint8_t>(((value >> 7) & 0x7f) + 32));
    out.push_back(static_cast<uint8_t>((value & 0x7f) + 32));
}

void write_gint3(std::vector<uint8_t>& out, int value) {
    out.push_back(static_cast<uint8_t>(((value >> 14) & 0x7f) + 32));
    out.push_back(static_cast<uint8_t>(((value >> 7) & 0x7f) + 32));
    out.push_back(static_cast<uint8_t>((value & 0x7f) + 32));
}

void write_gint5(std::vector<uint8_t>& out, int value) {
    out.push_back(static_cast<uint8_t>(((value >> 28) & 0x7f) + 32));
    out.push_back(static_cast<uint8_t>(((value >> 21) & 0x7f) + 32));
    out.push_back(static_cast<uint8_t>(((value >> 14) & 0x7f) + 32));
    out.push_back(static_cast<uint8_t>(((value >> 7) & 0x7f) + 32));
    out.push_back(static_cast<uint8_t>((value & 0x7f) + 32));
}

void write_gstring(std::vector<uint8_t>& out, const char* value) {
    std::string s = value ? value : "";
    if (s.size() > 223) s.resize(223);
    write_gchar(out, static_cast<int>(s.size()));
    out.insert(out.end(), s.begin(), s.end());
}

void write_position2(std::vector<uint8_t>& out, float tiles) {
    int pixels = static_cast<int>(std::floor((tiles * 16.0f) + 0.5f));
    int raw = pixels < 0 ? ((-pixels) << 1) | 1 : pixels << 1;
    write_gshort(out, raw);
}

std::string tclient_default_client_info(const TClient& client) {
    if (!client.client_info_override.empty()) return client.client_info_override;

    std::string platform = client.client_platform_override.empty()
        ? client.version.platform
        : client.client_platform_override;

    std::string info = platform;
    info += ",";
    info += client.client_id1;
    info += ",";
    info += client.client_id2;
    info += ",";
    info += client.client_id3;
    info += ",";
    info += client.client_id4;
    info += ",gclib";
    return info;
}
