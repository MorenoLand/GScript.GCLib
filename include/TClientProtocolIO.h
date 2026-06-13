#ifndef GCLIB_PROTOCOL_IO_H
#define GCLIB_PROTOCOL_IO_H

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

struct TClientVersionConfig {
    const char* name;
    const char* protocol;
    const char* build;
    int client_type;
    int generation;
    bool sends_build;
    const char* platform;
    const char* handshake;
};

struct TPacketReader {
    const uint8_t* data = nullptr;
    size_t len = 0;
    size_t pos = 0;

    explicit TPacketReader(const std::vector<uint8_t>& bytes) : data(bytes.data()), len(bytes.size()) {}
    TPacketReader(const uint8_t* bytes, size_t length) : data(bytes), len(length) {}

    bool has(size_t count = 1) const { return pos + count <= len; }
    uint8_t byte() { return has() ? data[pos++] : 0; }
    int gchar() { return std::max(0, static_cast<int>(byte()) - 32); }
    int gshort() {
        if (!has(2)) return 0;
        int a = static_cast<int>(data[pos++]) - 32;
        int b = static_cast<int>(data[pos++]) - 32;
        return (a << 7) | b;
    }
    int gint3() {
        if (!has(3)) return 0;
        int a = static_cast<int>(data[pos++]) - 32;
        int b = static_cast<int>(data[pos++]) - 32;
        int c = static_cast<int>(data[pos++]) - 32;
        return (a << 14) | (b << 7) | c;
    }
    int gint5() {
        if (!has(5)) return 0;
        int result = 0;
        for (int i = 0; i < 5; ++i) result = (result << 7) | (static_cast<int>(data[pos++]) - 32);
        return result;
    }
    std::string str(size_t count) {
        count = std::min(count, len - pos);
        std::string out(reinterpret_cast<const char*>(data + pos), count);
        pos += count;
        return out;
    }
    std::string gstring() { return str(static_cast<size_t>(gchar())); }
    std::vector<uint8_t> remaining() {
        std::vector<uint8_t> out(data + pos, data + len);
        pos = len;
        return out;
    }
};

void write_gchar(std::vector<uint8_t>& out, int value);
void write_gshort(std::vector<uint8_t>& out, int value);
void write_gint3(std::vector<uint8_t>& out, int value);
void write_gint5(std::vector<uint8_t>& out, int value);
void write_gstring(std::vector<uint8_t>& out, const char* value);
void write_position2(std::vector<uint8_t>& out, float tiles);
std::string tclient_default_client_info(const struct TClient& client);

#endif
