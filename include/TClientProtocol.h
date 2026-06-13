#ifndef GCLIB_TCLIENT_PROTOCOL_H
#define GCLIB_TCLIENT_PROTOCOL_H

#include "TClient.h"

#include <cstdint>
#include <vector>

struct TClientDecodedPacket {
    int packet_id;
    std::vector<uint8_t> payload;
};

struct TClientProtocolOps {
    const char* name;
    int generation;
    bool (*build_login_frame)(TClient* client, const char* account, const char* password, std::vector<uint8_t>& framed);
    bool (*build_packet_frame)(TClient* client, int packet_id, const uint8_t* data, int length, std::vector<uint8_t>& framed);
    bool (*decode_frame)(TClient* client, const std::vector<uint8_t>& frame, std::vector<uint8_t>& decrypted);
    bool (*decode_packets)(TClient* client, const std::vector<uint8_t>& frame, std::vector<TClientDecodedPacket>& packets);
};

const TClientProtocolOps* tclient_protocol_ops(const TClientVersionConfig& version);

#endif
