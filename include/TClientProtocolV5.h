#ifndef GCLIB_PROTOCOL_GEN5_H
#define GCLIB_PROTOCOL_GEN5_H

#include "TClient.h"

#include <cstdint>
#include <vector>

TClientVersionConfig tclient_resolve_gen5_version(const char* version);
bool tclient_gen5_build_login_frame(TClient* client, const char* account, const char* password, std::vector<uint8_t>& framed);
bool tclient_gen5_build_packet_frame(TClient* client, int packet_id, const uint8_t* data, int length, std::vector<uint8_t>& framed);
bool tclient_gen5_decode_frame(TClient* client, const std::vector<uint8_t>& frame, std::vector<uint8_t>& decrypted);

#endif
