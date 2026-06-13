#ifndef GCLIB_TCLIENT_PACKET_DISPATCH_H
#define GCLIB_TCLIENT_PACKET_DISPATCH_H

#include "TClient.h"

#include <cstdint>
#include <vector>

void tclient_dispatch_packet(TClient* client, int packet_id, const std::vector<uint8_t>& payload);
void tclient_flush_pending_level_capture(TClient* client, int packet_id);
void tclient_process_decrypted(TClient* client, const std::vector<uint8_t>& decrypted);

#endif
