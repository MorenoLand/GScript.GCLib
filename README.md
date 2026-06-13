# GCLib

`gclib` is a C ABI client-protocol library for building custom game clients, headless clients, tools, and protocol test harnesses. It owns the TCP connection, protocol-generation framing, login payloads, encryption/compression paths, packet send helpers, player-state cache, resource dumps, and common packet decoding so wrappers can stay thin.

Python, C#, Rust, or any other client should call typed functions and callbacks instead of building or parsing raw protocol packets itself.

Gen5 support includes classic framing, compression, encryption, login framing, packet framing, and common packet dispatch. Gen6 support includes the handshake/login path, RSA private-key based session setup, RC4 receive decrypt, compression decode, packet framing, and bundle unpacking.

`include/IEnums.h` is kept as the protocol enum source of truth.

## Build

Windows with CMake and Visual Studio:

```bat
cmake -S . -B build -A x64
cmake --build build --config Release
```

Linux/macOS with CMake:

```sh
cmake -S . -B build
cmake --build build --parallel
```

The build outputs the shared library to `bin/` in the repository root (`gclib.dll`, `gclib.so`, or `gclib.dylib`, depending on platform). Copy it next to the wrapper or application that loads it.

## Python Smoke Examples

```sh
python examples/python/smoke_exports.py
python examples/python/test_v5.py --host 127.0.0.1 --port 14900 --account account --password password
python examples/python/test_v6.py --host 127.0.0.1 --port 14720 --account account --password password
```

The examples use `ctypes` only for loading the C ABI. Packet framing, login payloads, compression, decoding, typed dispatch, and resource dumping stay inside the library.

For the settings-file based Gen6 smoke helper, copy the sample config and keep the real file untracked:

```sh
cp examples/python/settings.sample.ini settings.ini
python examples/python/connect_from_headless_config.py --settings settings.ini --account account --password password
```

The sample config contains only placeholders. Real endpoint details, identity seeds, and private key material should live in an ignored `settings.ini`, a `*.local.ini`, environment variables, or your wrapper's own config system.

## Minimal Flow

```c
GCHandle gc = gc_create("127.0.0.1", 14900, "6.037");
gc_set_client_identity(gc, "win", "", "", "", "");
gc_connect(gc);
gc_login(gc, "account", "password");
gc_send_level_chat(gc, "hello");
gc_destroy(gc);
```

For protocol generations that need an exact client-info string or certificate material, set those values before `gc_connect` / `gc_login`.

## Public Contract

- Wrappers should not parse frame bytes or build packet payloads for implemented behavior.
- `gc_on_packet_event` carries canonical packet names plus JSON for decoded packet families that do not yet need dedicated callbacks.
- `gc_on_resource` carries decoded resource bytes and can also write them to disk through the DLL-owned dump path.
- `gc_get_player_json` and `gc_get_self_player_json` read the DLL-owned player cache for UI binding.
- `gc_send_packet` is an explicit escape hatch for experiments and protocol gaps, not the normal integration path.
- Any `char*` returned by gclib must be released with `gc_free_string`.

## API Reference

All strings are `char*` / `const char*` encoded as protocol-compatible single-byte text unless a wrapper intentionally passes UTF-8-compatible content for its own UI. Callback pointers may be `NULL` to unregister.

### Connection And Login

| API | Purpose |
| --- | --- |
| `gc_create` | Creates a client by version string such as `2.22`, `5.07`, `6.037`, or `6.2`. |
| `gc_create_version` | Creates a client using the `GCVersion` enum. |
| `gc_destroy` | Disconnects, joins the receive thread, and frees the handle. |
| `gc_set_client_info` | Overrides the full login client-info string when a wrapper needs exact control. |
| `gc_set_client_identity` | Builds the login client-info string from platform and identity fields. |
| `gc_set_handshake` | Overrides the pre-login handshake string for protocol generations that require one. |
| `gc_set_encryption_certificate_pem` | Stores PEM certificate/key text used by encrypted-session protocols. |
| `gc_set_encryption_certificate_file` | Loads certificate/key text from a file path provided by the wrapper. |
| `gc_connect` | Opens the TCP connection and starts the receive loop. |
| `gc_login` | Sends the login frame through the selected protocol implementation. |
| `gc_disconnect` | Closes the socket and flushes any pending level capture. |
| `gc_is_connected` | Returns whether the socket is currently connected. |
| `gc_is_authenticated` | Returns whether login/authentication completed. |
| `gc_poll` | Lightweight status helper for wrappers that want a poll call. |
| `gc_get_last_error` | Returns the last library-level error string for the handle. |

### Callback Registration

| API | Purpose |
| --- | --- |
| `gc_on_connected` | Fires when the socket connects. |
| `gc_on_disconnected` | Fires when the server disconnects or the client closes. |
| `gc_on_authenticated` | Fires when the library considers the client authenticated. |
| `gc_on_raw_packet` | Optional diagnostic callback for every decoded packet body. |
| `gc_on_packet_event` | Fires with packet id, enum name, and JSON for decoded/fallback packet events. |
| `gc_on_chat` | Fires for player chat with player id and message text. |
| `gc_on_chat_ex` | Fires for chat with cached player identity fields: id, account, nick, community, level, and message. |
| `gc_on_private_message` | Fires for incoming PMs with sender id, message type, and text. |
| `gc_on_level_name` | Fires when the active level name changes. |
| `gc_on_player_warp` | Fires for basic level warps with x/y/level. |
| `gc_on_player_warp2` | Fires for extended warps with x/y/z, gmap coordinates, and level. |
| `gc_on_player_props` | Fires with named JSON fields for the local player. |
| `gc_on_other_player_props` | Fires with player id plus named JSON fields for another player. |
| `gc_on_player_left` | Fires when a player leaves the server/player list. |
| `gc_on_board_packet` | Fires with decoded tile values plus raw board packet bytes. |
| `gc_on_file` | Fires when a requested file/resource arrives. |
| `gc_on_file_failed` | Fires when a requested file/resource fails. |
| `gc_on_world_time` | Fires for world-time updates. |
| `gc_on_npc_props` | Fires with NPC id and named JSON fields for NPC properties. |
| `gc_on_npc_deleted` | Fires when an NPC delete packet arrives. |
| `gc_on_sign` | Fires with sign x/y/text. |
| `gc_on_explosion` | Fires with explosion x/y/radius/power. |
| `gc_on_hit_objects` | Fires with hit-object position, power, and player id when present. |
| `gc_on_server_text` | Fires with server text replies from request/send-text operations. |
| `gc_on_flag_set` | Fires when a client flag is set or updated. |
| `gc_on_flag_del` | Fires when a client flag is deleted. |
| `gc_on_weapon_script` | Fires for received weapon/class-style bytecode payloads. |
| `gc_on_resource` | Fires for dumpable resources such as level snapshots, files, scripts, gani data, and bytecode blobs. |

### Cached State And Resource Dumps

| API | Purpose |
| --- | --- |
| `gc_get_player_json` | Copies cached player state for one player id as JSON. |
| `gc_get_self_player_json` | Copies cached local-player state as JSON. |
| `gc_set_resource_dump_directory` | Enables DLL-owned resource dumping to a directory. |
| `gc_set_resource_dump_types` | Filters dumped resources by comma-separated types such as `level,file,npc-bytecode`. |
| `gc_free_string` | Frees strings returned by cache/state APIs. |

Resource dump filenames use portable percent-encoded path segments so names containing Windows-hostile characters can still be written safely.

### Chat, PMs, And Player Text

| API | Purpose |
| --- | --- |
| `gc_send_chat` | Sends normal visible chat through the level-chat path. |
| `gc_send_level_chat` | Sends normal visible level chat. |
| `gc_send_toall_chat` | Sends the legacy to-all/minimap chat packet. Most clients should not use this for normal chat. |
| `gc_send_private_message` | Sends one PM packet to one or more player ids. |
| `gc_request_text` | Requests a server text value using the client comma-text payload shape. |
| `gc_send_text` | Updates a server text value using the client comma-text payload shape. |

### Player Properties, Movement, And Appearance

| API | Purpose |
| --- | --- |
| `gc_set_player_prop_string` | Sends a string-valued player property update by property id. |
| `gc_set_player_prop_byte` | Sends a byte-valued player property update by property id. |
| `gc_set_nickname` | Updates the local nickname property. |
| `gc_set_head_image` | Updates the local head image property. |
| `gc_set_body_image` | Updates the local body image property. |
| `gc_set_player_attribute` | Updates one player attribute slot. |
| `gc_set_player_status` | Updates the player status byte. |
| `gc_set_player_colors` | Updates skin, coat, sleeves, shoes, and belt colors. |
| `gc_move` | Sends a position/direction update with optional high-precision coordinates. |
| `gc_walk_step` | Convenience movement helper that sends animation, direction, and position together. |
| `gc_level_warp` | Sends a level warp request with x/y/level. |
| `gc_set_animation` | Sends animation/gani, position, and direction as player properties. |

### Files, Levels, Scripts, And Resources

| API | Purpose |
| --- | --- |
| `gc_request_file` | Requests a public downloadable resource by filename. |
| `gc_request_level_board` | Requests board data for a level rectangle. |
| `gc_request_weapon_script` | Requests a weapon script/bytecode payload. |
| `gc_request_class_script` | Requests a class script/bytecode payload with a mod-time hint. |
| `gc_request_gani_script` | Requests a gani script/resource with a mod-time hint. |

Level editing files are not arbitrary client downloads. Current-level `.nw` snapshots are assembled from level packets received while standing in the level; level NPC bytecode is embedded inside the emitted NPC block as a `//#GCLIB_BYTECODE_BASE64` marker.

### Flags And Diagnostics

| API | Purpose |
| --- | --- |
| `gc_set_flag` | Sets or updates a client flag. |
| `gc_unset_flag` | Deletes a client flag. |
| `gc_send_packet` | Sends an explicitly supplied packet id and payload for diagnostics or unimplemented protocol experiments. |

### Packet Event JSON

`gc_on_packet_event` uses stable enum names from `IEnums.h`. Richer packet families emit named JSON fields. Remaining diagnostic categories include:

| Category | Meaning |
| --- | --- |
| `world-object` | Horse, baddy, NPC action, or similar world-object payload not yet promoted to a dedicated callback. |
| `trigger-action` | Raw trigger-action payload. |
| `motion-or-action` | Movement, shooting, process list, or update action payloads that still need deeper protocol-specific decoding. |
| `ui` | UI/window payloads. |
| `unknown` | Known enum slot with unknown structure. |
| `unhandled` | Fallback for packet ids without a richer handler. |

## Layout

- `include/gclib.h` is the stable C ABI.
- `include/IEnums.h` is the packet/property enum source of truth.
- `include/TClient*.h` contains internal client/protocol interfaces used by the library build.
- `src/gclib.cpp` owns the public C ABI and typed send helpers.
- `src/TClientPacketDispatch.cpp` owns packet parsing, state cache updates, callbacks, and resource dumping.
- `src/TClientProtocolV5.cpp` owns Gen5 frame compression, encryption, login framing, and packet framing.
- `src/TClientProtocolV6.cpp` owns Gen6 headers, handshake login framing, compression decode, packet framing, RC4 receive decrypt, and bundle unpacking.
- `src/TClientProtocolIO.cpp` owns shared G-type encoding helpers.

## Third-Party Code

Bundled compression sources live under `deps/` so the shared library can build without external package setup. See `THIRD_PARTY_NOTICES.md`.
