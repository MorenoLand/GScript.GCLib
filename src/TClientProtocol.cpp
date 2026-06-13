#include "TClientProtocol.h"

#include "TClientProtocolV5.h"
#include "TClientProtocolV6.h"

namespace {

static const TClientProtocolOps protocol_v5 = {
    "TClientProtocolV5",
    5,
    tclient_gen5_build_login_frame,
    tclient_gen5_build_packet_frame,
    tclient_gen5_decode_frame,
    nullptr
};

static const TClientProtocolOps protocol_v6 = {
    "TClientProtocolV6",
    6,
    tclient_v6_build_login_frame,
    tclient_v6_build_packet_frame,
    nullptr,
    tclient_v6_decode_packets
};

} // namespace

const TClientProtocolOps* tclient_protocol_ops(const TClientVersionConfig& version) {
    if (version.generation == 5) return &protocol_v5;
    if (version.generation == 6) return &protocol_v6;
    return nullptr;
}
