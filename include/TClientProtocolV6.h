#ifndef GCLIB_TCLIENT_PROTOCOL_V6_H
#define GCLIB_TCLIENT_PROTOCOL_V6_H

#include "TClient.h"
#include "TClientProtocol.h"

#include <cstdint>
#include <vector>

bool tclient_v6_build_login_frame(TClient* client, const char* account, const char* password, std::vector<uint8_t>& framed);
bool tclient_v6_build_packet_frame(TClient* client, int packet_id, const uint8_t* data, int length, std::vector<uint8_t>& framed);
bool tclient_v6_decode_frame(TClient* client, const std::vector<uint8_t>& frame, std::vector<uint8_t>& decrypted);
bool tclient_v6_decode_packets(TClient* client, const std::vector<uint8_t>& frame, std::vector<TClientDecodedPacket>& packets);
void tclient_v6_decrypt_stream(TClient* client, uint8_t* data, size_t length);

#endif
