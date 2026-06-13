#ifndef GCLIB_CLIENT_INTERNAL_H
#define GCLIB_CLIENT_INTERNAL_H

#include "gclib.h"
#include "TClientSocket.h"
#include "TClientProtocolIO.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct CallbackConnected { GC_OnConnected cb = nullptr; void* ud = nullptr; };
struct CallbackDisconnected { GC_OnDisconnected cb = nullptr; void* ud = nullptr; };
struct CallbackAuthenticated { GC_OnAuthenticated cb = nullptr; void* ud = nullptr; };
struct CallbackRawPacket { GC_OnRawPacket cb = nullptr; void* ud = nullptr; };
struct CallbackPacketEvent { GC_OnPacketEvent cb = nullptr; void* ud = nullptr; };
struct CallbackChat { GC_OnChat cb = nullptr; void* ud = nullptr; };
struct CallbackChatEx { GC_OnChatEx cb = nullptr; void* ud = nullptr; };
struct CallbackPrivateMessage { GC_OnPrivateMessage cb = nullptr; void* ud = nullptr; };
struct CallbackLevelName { GC_OnLevelName cb = nullptr; void* ud = nullptr; };
struct CallbackPlayerWarp { GC_OnPlayerWarp cb = nullptr; void* ud = nullptr; };
struct CallbackPlayerWarp2 { GC_OnPlayerWarp2 cb = nullptr; void* ud = nullptr; };
struct CallbackPlayerProps { GC_OnPlayerProps cb = nullptr; void* ud = nullptr; };
struct CallbackOtherPlayerProps { GC_OnOtherPlayerProps cb = nullptr; void* ud = nullptr; };
struct CallbackPlayerLeft { GC_OnPlayerLeft cb = nullptr; void* ud = nullptr; };
struct CallbackBoardPacket { GC_OnBoardPacket cb = nullptr; void* ud = nullptr; };
struct CallbackFile { GC_OnFile cb = nullptr; void* ud = nullptr; };
struct CallbackFileFailed { GC_OnFileFailed cb = nullptr; void* ud = nullptr; };
struct CallbackWorldTime { GC_OnWorldTime cb = nullptr; void* ud = nullptr; };
struct CallbackNpcProps { GC_OnNpcProps cb = nullptr; void* ud = nullptr; };
struct CallbackNpcDeleted { GC_OnNpcDeleted cb = nullptr; void* ud = nullptr; };
struct CallbackSign { GC_OnSign cb = nullptr; void* ud = nullptr; };
struct CallbackExplosion { GC_OnExplosion cb = nullptr; void* ud = nullptr; };
struct CallbackHitObjects { GC_OnHitObjects cb = nullptr; void* ud = nullptr; };
struct CallbackServerText { GC_OnServerText cb = nullptr; void* ud = nullptr; };
struct CallbackFlagSet { GC_OnFlagSet cb = nullptr; void* ud = nullptr; };
struct CallbackFlagDel { GC_OnFlagDel cb = nullptr; void* ud = nullptr; };
struct CallbackWeaponScript { GC_OnWeaponScript cb = nullptr; void* ud = nullptr; };
struct CallbackResource { GC_OnResource cb = nullptr; void* ud = nullptr; };
struct TClientProtocolOps;

struct TClientPlayerState {
    int id = -1;
    bool connected = true;
    std::unordered_map<std::string, std::string> fields;
};

struct TClientLevelNpcState {
    int id = -1;
    std::string image = "-";
    double x = 0.0;
    double y = 0.0;
    std::vector<uint8_t> bytecode;
};

struct TClientLevelState {
    bool active = false;
    bool has_tiles = false;
    std::string name;
    std::vector<unsigned short> tiles;
    std::vector<std::string> links;
    std::unordered_map<int, TClientLevelNpcState> npcs;
};

struct TClient {
    std::string host;
    int port = 0;
    TClientVersionConfig version{};
    const TClientProtocolOps* protocol = nullptr;
    std::string client_info_override;
    std::string client_platform_override;
    std::string client_id1;
    std::string client_id2;
    std::string client_id3;
    std::string client_id4;
    std::string encryption_certificate_pem;
    std::string handshake_override;
    std::string last_error;
    int enc_key = 0;
    uint8_t packet_index = 0;
    bool v6_incoming_crypto_enabled = false;
    uint8_t v6_rc4_s[256]{};
    uint8_t v6_rc4_i = 0;
    uint8_t v6_rc4_j = 0;
    uint32_t in_iter = 0x04A80B38;
    uint32_t out_iter = 0x04A80B38;
    std::atomic<bool> connected{false};
    std::atomic<bool> authenticated{false};
    std::atomic<bool> stop{false};
    std::atomic<bool> first_packet{true};
    socket_t sock = invalid_socket_value;
    std::thread recv_thread;
    std::mutex send_mutex;
    std::mutex cb_mutex;
    std::mutex players_mutex;
    std::vector<uint8_t> recv_buffer;
    int raw_expected = 0;
    std::vector<uint8_t> raw_buffer;
    std::string resource_dump_directory;
    std::unordered_set<std::string> resource_dump_types;
    std::string current_level;
    TClientLevelState level_state;
    int self_player_id = -1;
    std::unordered_map<int, TClientPlayerState> players;

    CallbackConnected on_connected;
    CallbackDisconnected on_disconnected;
    CallbackAuthenticated on_authenticated;
    CallbackRawPacket on_raw_packet;
    CallbackPacketEvent on_packet_event;
    CallbackChat on_chat;
    CallbackChatEx on_chat_ex;
    CallbackPrivateMessage on_private_message;
    CallbackLevelName on_level_name;
    CallbackPlayerWarp on_player_warp;
    CallbackPlayerWarp2 on_player_warp2;
    CallbackPlayerProps on_player_props;
    CallbackOtherPlayerProps on_other_player_props;
    CallbackPlayerLeft on_player_left;
    CallbackBoardPacket on_board_packet;
    CallbackFile on_file;
    CallbackFileFailed on_file_failed;
    CallbackWorldTime on_world_time;
    CallbackNpcProps on_npc_props;
    CallbackNpcDeleted on_npc_deleted;
    CallbackSign on_sign;
    CallbackExplosion on_explosion;
    CallbackHitObjects on_hit_objects;
    CallbackServerText on_server_text;
    CallbackFlagSet on_flag_set;
    CallbackFlagDel on_flag_del;
    CallbackWeaponScript on_weapon_script;
    CallbackResource on_resource;
};

#endif
