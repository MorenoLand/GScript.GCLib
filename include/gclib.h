#ifndef GCLIB_H
#define GCLIB_H

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _WIN32
#ifdef GCLIB_EXPORTS
#define GCLIB_API __declspec(dllexport)
#else
#define GCLIB_API __declspec(dllimport)
#endif
#else
#define GCLIB_API
#endif

typedef void* GCHandle;

typedef enum {
    GC_VERSION_222 = 0,
    GC_VERSION_6037 = 1,
    GC_VERSION_6037_LINUX = 2,
    GC_VERSION_62 = 3
} GCVersion;

typedef void (*GC_OnConnected)(void* user_data);
typedef void (*GC_OnDisconnected)(const char* reason, void* user_data);
typedef void (*GC_OnAuthenticated)(void* user_data);
typedef void (*GC_OnRawPacket)(int packet_id, const void* data, int length, void* user_data);
typedef void (*GC_OnPacketEvent)(int packet_id, const char* packet_name, const char* event_json, void* user_data);
typedef void (*GC_OnChat)(int player_id, const char* message, void* user_data);
typedef void (*GC_OnChatEx)(int player_id, const char* account, const char* nickname, const char* community, const char* level, const char* message, void* user_data);
typedef void (*GC_OnPrivateMessage)(int player_id, const char* message_type, const char* message, void* user_data);
typedef void (*GC_OnLevelName)(const char* level, void* user_data);
typedef void (*GC_OnPlayerWarp)(float x, float y, const char* level, void* user_data);
typedef void (*GC_OnPlayerWarp2)(float x, float y, int z, int gmap_x, int gmap_y, const char* level, void* user_data);
typedef void (*GC_OnPlayerProps)(const char* props_json, void* user_data);
typedef void (*GC_OnOtherPlayerProps)(int player_id, const char* props_json, void* user_data);
typedef void (*GC_OnPlayerLeft)(int player_id, void* user_data);
typedef void (*GC_OnBoardPacket)(const unsigned short* tiles, int count, const void* raw, int raw_length, void* user_data);
typedef void (*GC_OnFile)(const char* filename, const void* data, int length, int mod_time, void* user_data);
typedef void (*GC_OnFileFailed)(const char* filename, void* user_data);
typedef void (*GC_OnWorldTime)(int world_time, void* user_data);
typedef void (*GC_OnNpcProps)(int npc_id, const char* props_json, void* user_data);
typedef void (*GC_OnNpcDeleted)(int npc_id, void* user_data);
typedef void (*GC_OnSign)(float x, float y, const char* text, void* user_data);
typedef void (*GC_OnExplosion)(float x, float y, int radius, int power, void* user_data);
typedef void (*GC_OnHitObjects)(float x, float y, int power, int player_id, void* user_data);
typedef void (*GC_OnServerText)(const char* text, void* user_data);
typedef void (*GC_OnFlagSet)(const char* name, const char* value, void* user_data);
typedef void (*GC_OnFlagDel)(const char* name, void* user_data);
typedef void (*GC_OnWeaponScript)(const char* script_type, const char* script_name, const void* bytecode, int length, void* user_data);
typedef void (*GC_OnResource)(const char* resource_type, const char* name, const void* data, int length, int packet_id, void* user_data);

GCLIB_API GCHandle gc_create(const char* host, int port, const char* version);
GCLIB_API GCHandle gc_create_version(const char* host, int port, GCVersion version);
GCLIB_API void gc_destroy(GCHandle handle);
GCLIB_API void gc_set_client_info(GCHandle handle, const char* client_info);
GCLIB_API void gc_set_client_identity(GCHandle handle, const char* platform, const char* id1, const char* id2, const char* id3, const char* id4);
GCLIB_API void gc_set_handshake(GCHandle handle, const char* handshake);
GCLIB_API void gc_set_encryption_certificate_pem(GCHandle handle, const char* certificate_pem);
GCLIB_API int gc_set_encryption_certificate_file(GCHandle handle, const char* path);
GCLIB_API int gc_connect(GCHandle handle);
GCLIB_API int gc_login(GCHandle handle, const char* account, const char* password);
GCLIB_API void gc_disconnect(GCHandle handle);
GCLIB_API int gc_is_connected(GCHandle handle);
GCLIB_API int gc_is_authenticated(GCHandle handle);
GCLIB_API int gc_poll(GCHandle handle);
GCLIB_API const char* gc_get_last_error(GCHandle handle);
GCLIB_API char* gc_get_player_json(GCHandle handle, int player_id);
GCLIB_API char* gc_get_self_player_json(GCHandle handle);
GCLIB_API int gc_set_resource_dump_directory(GCHandle handle, const char* directory);
GCLIB_API int gc_set_resource_dump_types(GCHandle handle, const char* comma_separated_types);

GCLIB_API void gc_on_connected(GCHandle handle, GC_OnConnected callback, void* user_data);
GCLIB_API void gc_on_disconnected(GCHandle handle, GC_OnDisconnected callback, void* user_data);
GCLIB_API void gc_on_authenticated(GCHandle handle, GC_OnAuthenticated callback, void* user_data);
GCLIB_API void gc_on_raw_packet(GCHandle handle, GC_OnRawPacket callback, void* user_data);
GCLIB_API void gc_on_packet_event(GCHandle handle, GC_OnPacketEvent callback, void* user_data);
GCLIB_API void gc_on_chat(GCHandle handle, GC_OnChat callback, void* user_data);
GCLIB_API void gc_on_chat_ex(GCHandle handle, GC_OnChatEx callback, void* user_data);
GCLIB_API void gc_on_private_message(GCHandle handle, GC_OnPrivateMessage callback, void* user_data);
GCLIB_API void gc_on_level_name(GCHandle handle, GC_OnLevelName callback, void* user_data);
GCLIB_API void gc_on_player_warp(GCHandle handle, GC_OnPlayerWarp callback, void* user_data);
GCLIB_API void gc_on_player_warp2(GCHandle handle, GC_OnPlayerWarp2 callback, void* user_data);
GCLIB_API void gc_on_player_props(GCHandle handle, GC_OnPlayerProps callback, void* user_data);
GCLIB_API void gc_on_other_player_props(GCHandle handle, GC_OnOtherPlayerProps callback, void* user_data);
GCLIB_API void gc_on_player_left(GCHandle handle, GC_OnPlayerLeft callback, void* user_data);
GCLIB_API void gc_on_board_packet(GCHandle handle, GC_OnBoardPacket callback, void* user_data);
GCLIB_API void gc_on_file(GCHandle handle, GC_OnFile callback, void* user_data);
GCLIB_API void gc_on_file_failed(GCHandle handle, GC_OnFileFailed callback, void* user_data);
GCLIB_API void gc_on_world_time(GCHandle handle, GC_OnWorldTime callback, void* user_data);
GCLIB_API void gc_on_npc_props(GCHandle handle, GC_OnNpcProps callback, void* user_data);
GCLIB_API void gc_on_npc_deleted(GCHandle handle, GC_OnNpcDeleted callback, void* user_data);
GCLIB_API void gc_on_sign(GCHandle handle, GC_OnSign callback, void* user_data);
GCLIB_API void gc_on_explosion(GCHandle handle, GC_OnExplosion callback, void* user_data);
GCLIB_API void gc_on_hit_objects(GCHandle handle, GC_OnHitObjects callback, void* user_data);
GCLIB_API void gc_on_server_text(GCHandle handle, GC_OnServerText callback, void* user_data);
GCLIB_API void gc_on_flag_set(GCHandle handle, GC_OnFlagSet callback, void* user_data);
GCLIB_API void gc_on_flag_del(GCHandle handle, GC_OnFlagDel callback, void* user_data);
GCLIB_API void gc_on_weapon_script(GCHandle handle, GC_OnWeaponScript callback, void* user_data);
GCLIB_API void gc_on_resource(GCHandle handle, GC_OnResource callback, void* user_data);

GCLIB_API int gc_send_packet(GCHandle handle, int packet_id, const void* data, int length);
GCLIB_API int gc_send_chat(GCHandle handle, const char* message);
GCLIB_API int gc_send_level_chat(GCHandle handle, const char* message);
GCLIB_API int gc_send_toall_chat(GCHandle handle, const char* message);
GCLIB_API int gc_set_player_prop_string(GCHandle handle, int prop_id, const char* value);
GCLIB_API int gc_set_player_prop_byte(GCHandle handle, int prop_id, int value);
GCLIB_API int gc_set_nickname(GCHandle handle, const char* nickname);
GCLIB_API int gc_set_head_image(GCHandle handle, const char* image);
GCLIB_API int gc_set_body_image(GCHandle handle, const char* image);
GCLIB_API int gc_set_player_attribute(GCHandle handle, int attribute_index, const char* value);
GCLIB_API int gc_set_player_status(GCHandle handle, int status);
GCLIB_API int gc_set_player_colors(GCHandle handle, int skin, int coat, int sleeves, int shoes, int belt);
GCLIB_API int gc_move(GCHandle handle, float x, float y, int direction, int use_high_precision);
GCLIB_API int gc_walk_step(GCHandle handle, float x, float y, int direction, const char* gani, int use_high_precision);
GCLIB_API int gc_level_warp(GCHandle handle, float x, float y, const char* level);
GCLIB_API int gc_set_animation(GCHandle handle, const char* gani, float x, float y, int direction);
GCLIB_API int gc_request_file(GCHandle handle, const char* filename);
GCLIB_API int gc_request_level_board(GCHandle handle, const char* level, int mod_time, int x, int y, int width, int height);
GCLIB_API int gc_send_private_message(GCHandle handle, const int* player_ids, int count, const char* message);
GCLIB_API int gc_set_flag(GCHandle handle, const char* flag);
GCLIB_API int gc_unset_flag(GCHandle handle, const char* flag);
GCLIB_API int gc_request_text(GCHandle handle, const char* type, const char* key, const char* default_value);
GCLIB_API int gc_send_text(GCHandle handle, const char* type, const char* key, const char* metadata, const char* value);
GCLIB_API int gc_request_weapon_script(GCHandle handle, const char* name);
GCLIB_API int gc_request_class_script(GCHandle handle, const char* name, int mod_time);
GCLIB_API int gc_request_gani_script(GCHandle handle, const char* name, int mod_time);

GCLIB_API void gc_free_string(char* value);

#ifdef __cplusplus
}
#endif

#endif
