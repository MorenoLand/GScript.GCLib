import argparse
import ctypes
import json
import math
import pathlib
import platform
import sys
import threading
import time

for stream in (sys.stdout, sys.stderr):
    if hasattr(stream, "reconfigure"):
        stream.reconfigure(errors="replace")

ROOT = pathlib.Path(__file__).resolve().parents[2]
LIB_NAME = {
    "Windows": "gclib.dll",
    "Darwin": "gclib.dylib",
}.get(platform.system(), "gclib.so")
DLL_PATH = ROOT / "bin" / LIB_NAME

GC_VERSION_222 = 0
GC_VERSION_6037 = 1
GC_VERSION_6037_LINUX = 2
GC_VERSION_62 = 3


class GCLib:
    def __init__(self, dll_path=DLL_PATH):
        self.lib = ctypes.CDLL(str(dll_path))
        self._callbacks = []
        self._bind()

    def _bind(self):
        c_void_p = ctypes.c_void_p
        c_char_p = ctypes.c_char_p
        c_int = ctypes.c_int

        self.lib.gc_create_version.argtypes = [c_char_p, c_int, c_int]
        self.lib.gc_create_version.restype = c_void_p
        self.lib.gc_destroy.argtypes = [c_void_p]
        self.lib.gc_destroy.restype = None
        self.lib.gc_connect.argtypes = [c_void_p]
        self.lib.gc_connect.restype = c_int
        self.lib.gc_login.argtypes = [c_void_p, c_char_p, c_char_p]
        self.lib.gc_login.restype = c_int
        self.lib.gc_disconnect.argtypes = [c_void_p]
        self.lib.gc_disconnect.restype = None
        self.lib.gc_is_connected.argtypes = [c_void_p]
        self.lib.gc_is_connected.restype = c_int
        self.lib.gc_is_authenticated.argtypes = [c_void_p]
        self.lib.gc_is_authenticated.restype = c_int
        self.lib.gc_get_last_error.argtypes = [c_void_p]
        self.lib.gc_get_last_error.restype = c_char_p
        self.lib.gc_get_player_json.argtypes = [c_void_p, c_int]
        self.lib.gc_get_player_json.restype = c_void_p
        self.lib.gc_get_self_player_json.argtypes = [c_void_p]
        self.lib.gc_get_self_player_json.restype = c_void_p
        self.lib.gc_set_resource_dump_directory.argtypes = [c_void_p, c_char_p]
        self.lib.gc_set_resource_dump_directory.restype = c_int
        self.lib.gc_set_resource_dump_types.argtypes = [c_void_p, c_char_p]
        self.lib.gc_set_resource_dump_types.restype = c_int
        self.lib.gc_free_string.argtypes = [c_void_p]
        self.lib.gc_free_string.restype = None
        self.lib.gc_set_client_info.argtypes = [c_void_p, c_char_p]
        self.lib.gc_set_client_info.restype = None
        self.lib.gc_set_client_identity.argtypes = [c_void_p, c_char_p, c_char_p, c_char_p, c_char_p, c_char_p]
        self.lib.gc_set_client_identity.restype = None
        self.lib.gc_set_handshake.argtypes = [c_void_p, c_char_p]
        self.lib.gc_set_handshake.restype = None
        self.lib.gc_set_encryption_certificate_pem.argtypes = [c_void_p, c_char_p]
        self.lib.gc_set_encryption_certificate_pem.restype = None
        self.lib.gc_set_encryption_certificate_file.argtypes = [c_void_p, c_char_p]
        self.lib.gc_set_encryption_certificate_file.restype = c_int
        self.lib.gc_set_nickname.argtypes = [c_void_p, c_char_p]
        self.lib.gc_set_nickname.restype = c_int
        self.lib.gc_move.argtypes = [c_void_p, ctypes.c_float, ctypes.c_float, c_int, c_int]
        self.lib.gc_move.restype = c_int
        self.lib.gc_walk_step.argtypes = [c_void_p, ctypes.c_float, ctypes.c_float, c_int, c_char_p, c_int]
        self.lib.gc_walk_step.restype = c_int
        self.lib.gc_level_warp.argtypes = [c_void_p, ctypes.c_float, ctypes.c_float, c_char_p]
        self.lib.gc_level_warp.restype = c_int
        self.lib.gc_send_chat.argtypes = [c_void_p, c_char_p]
        self.lib.gc_send_chat.restype = c_int
        self.lib.gc_send_level_chat.argtypes = [c_void_p, c_char_p]
        self.lib.gc_send_level_chat.restype = c_int
        self.lib.gc_send_toall_chat.argtypes = [c_void_p, c_char_p]
        self.lib.gc_send_toall_chat.restype = c_int
        self.lib.gc_request_file.argtypes = [c_void_p, c_char_p]
        self.lib.gc_request_file.restype = c_int
        self.lib.gc_request_level_board.argtypes = [c_void_p, c_char_p, c_int, c_int, c_int, c_int, c_int]
        self.lib.gc_request_level_board.restype = c_int
        self.lib.gc_set_flag.argtypes = [c_void_p, c_char_p]
        self.lib.gc_set_flag.restype = c_int
        self.lib.gc_unset_flag.argtypes = [c_void_p, c_char_p]
        self.lib.gc_unset_flag.restype = c_int
        self.lib.gc_request_text.argtypes = [c_void_p, c_char_p, c_char_p, c_char_p]
        self.lib.gc_request_text.restype = c_int
        self.lib.gc_send_text.argtypes = [c_void_p, c_char_p, c_char_p, c_char_p, c_char_p]
        self.lib.gc_send_text.restype = c_int
        self.lib.gc_request_weapon_script.argtypes = [c_void_p, c_char_p]
        self.lib.gc_request_weapon_script.restype = c_int
        self.lib.gc_request_class_script.argtypes = [c_void_p, c_char_p, c_int]
        self.lib.gc_request_class_script.restype = c_int
        self.lib.gc_request_gani_script.argtypes = [c_void_p, c_char_p, c_int]
        self.lib.gc_request_gani_script.restype = c_int

        self.CB_CONNECTED = ctypes.CFUNCTYPE(None, c_void_p)
        self.CB_DISCONNECTED = ctypes.CFUNCTYPE(None, c_char_p, c_void_p)
        self.CB_AUTHENTICATED = ctypes.CFUNCTYPE(None, c_void_p)
        self.CB_RAW = ctypes.CFUNCTYPE(None, c_int, c_void_p, c_int, c_void_p)
        self.CB_PACKET_EVENT = ctypes.CFUNCTYPE(None, c_int, c_char_p, c_char_p, c_void_p)
        self.CB_CHAT = ctypes.CFUNCTYPE(None, c_int, c_char_p, c_void_p)
        self.CB_CHAT_EX = ctypes.CFUNCTYPE(None, c_int, c_char_p, c_char_p, c_char_p, c_char_p, c_char_p, c_void_p)
        self.CB_PRIVATE_MESSAGE = ctypes.CFUNCTYPE(None, c_int, c_char_p, c_char_p, c_void_p)
        self.CB_LEVEL_NAME = ctypes.CFUNCTYPE(None, c_char_p, c_void_p)
        self.CB_PLAYER_PROPS = ctypes.CFUNCTYPE(None, c_char_p, c_void_p)
        self.CB_OTHER_PLAYER_PROPS = ctypes.CFUNCTYPE(None, c_int, c_char_p, c_void_p)
        self.CB_SERVER_TEXT = ctypes.CFUNCTYPE(None, c_char_p, c_void_p)
        self.CB_FLAG_SET = ctypes.CFUNCTYPE(None, c_char_p, c_char_p, c_void_p)
        self.CB_FLAG_DEL = ctypes.CFUNCTYPE(None, c_char_p, c_void_p)
        self.CB_WEAPON_SCRIPT = ctypes.CFUNCTYPE(None, c_char_p, c_char_p, c_void_p, c_int, c_void_p)
        self.CB_RESOURCE = ctypes.CFUNCTYPE(None, c_char_p, c_char_p, c_void_p, c_int, c_int, c_void_p)

        self.lib.gc_on_connected.argtypes = [c_void_p, self.CB_CONNECTED, c_void_p]
        self.lib.gc_on_disconnected.argtypes = [c_void_p, self.CB_DISCONNECTED, c_void_p]
        self.lib.gc_on_authenticated.argtypes = [c_void_p, self.CB_AUTHENTICATED, c_void_p]
        self.lib.gc_on_raw_packet.argtypes = [c_void_p, self.CB_RAW, c_void_p]
        self.lib.gc_on_packet_event.argtypes = [c_void_p, self.CB_PACKET_EVENT, c_void_p]
        self.lib.gc_on_chat.argtypes = [c_void_p, self.CB_CHAT, c_void_p]
        self.lib.gc_on_chat_ex.argtypes = [c_void_p, self.CB_CHAT_EX, c_void_p]
        self.lib.gc_on_private_message.argtypes = [c_void_p, self.CB_PRIVATE_MESSAGE, c_void_p]
        self.lib.gc_on_level_name.argtypes = [c_void_p, self.CB_LEVEL_NAME, c_void_p]
        self.lib.gc_on_player_props.argtypes = [c_void_p, self.CB_PLAYER_PROPS, c_void_p]
        self.lib.gc_on_other_player_props.argtypes = [c_void_p, self.CB_OTHER_PLAYER_PROPS, c_void_p]
        self.lib.gc_on_server_text.argtypes = [c_void_p, self.CB_SERVER_TEXT, c_void_p]
        self.lib.gc_on_flag_set.argtypes = [c_void_p, self.CB_FLAG_SET, c_void_p]
        self.lib.gc_on_flag_del.argtypes = [c_void_p, self.CB_FLAG_DEL, c_void_p]
        self.lib.gc_on_weapon_script.argtypes = [c_void_p, self.CB_WEAPON_SCRIPT, c_void_p]
        self.lib.gc_on_resource.argtypes = [c_void_p, self.CB_RESOURCE, c_void_p]

    def keep(self, callback):
        self._callbacks.append(callback)
        return callback


def b(value):
    return value.encode("utf-8")


def text(value):
    return value.decode("utf-8", "replace") if value else ""


def take_string(gc, ptr):
    if not ptr:
        return ""
    try:
        return ctypes.cast(ptr, ctypes.c_char_p).value.decode("utf-8", "replace")
    finally:
        gc.lib.gc_free_string(ptr)


def parse_move_arg(value):
    parts = [part.strip() for part in value.split(",")]
    if len(parts) < 3:
        raise ValueError("expected x,y,dir[,precise]")
    x = float(parts[0])
    y = float(parts[1])
    direction = int(parts[2])
    precise = int(parts[3]) if len(parts) > 3 else 1
    return x, y, direction, precise


def parse_warp_arg(value):
    parts = [part.strip() for part in value.split(",")]
    if len(parts) != 3:
        raise ValueError("expected level,x,y")
    return parts[0], float(parts[1]), float(parts[2])


def make_parser(description, default_version):
    parser = argparse.ArgumentParser(description=description)
    parser.add_argument("--host", required=True)
    parser.add_argument("--port", required=True, type=int)
    parser.add_argument("--account", required=True)
    parser.add_argument("--password", required=True)
    parser.add_argument("--platform", default="win")
    parser.add_argument("--id1", default="")
    parser.add_argument("--id2", default="")
    parser.add_argument("--id3", default="")
    parser.add_argument("--id4", default="")
    parser.add_argument("--nick", default="GCLibTest")
    parser.add_argument("--chat", default="")
    parser.add_argument("--level-chat", default="")
    parser.add_argument("--toall-chat", default="")
    parser.add_argument("--warp-level", default="", help="Warp as level,x,y before optional board request")
    parser.add_argument("--move", default="", help="Send one move as x,y,dir[,precise]")
    parser.add_argument("--walk", default="", help="Walk toward x,y[,step,delay,precise,gani]")
    parser.add_argument("--show-cache", action="store_true", help="Print cached player JSON snapshots from the DLL")
    parser.add_argument("--dump-resources", default="", help="Directory for DLL-side resource dumps")
    parser.add_argument("--dump-resource-types", default="", help="Comma-separated resource types to dump, such as level,level-board,file")
    parser.add_argument("--request-file", action="append", default=[], help="Request a server file; may be passed more than once")
    parser.add_argument("--request-level-board", nargs="?", const="", default=None, help="Request board data for this level, or current level when omitted")
    parser.add_argument("--seconds", default=15, type=int)
    parser.add_argument("--version", default=default_version)
    return parser


def run_basic_client(args, version):
    gc = GCLib()
    handle = gc.lib.gc_create_version(b(args.host), args.port, version)
    if not handle:
        raise RuntimeError("gc_create_version failed")

    if getattr(args, "dump_resources", ""):
        if not gc.lib.gc_set_resource_dump_directory(handle, b(args.dump_resources)):
            raise RuntimeError("gc_set_resource_dump_directory failed")
    if getattr(args, "dump_resource_types", ""):
        if not gc.lib.gc_set_resource_dump_types(handle, b(args.dump_resource_types)):
            raise RuntimeError("gc_set_resource_dump_types failed")
    startup_sent = False
    move_sent = False
    walk_sent = False
    level_board_requested = False
    self_player_id = None

    if getattr(args, "client_info", ""):
        gc.lib.gc_set_client_info(handle, b(args.client_info))
    else:
        gc.lib.gc_set_client_identity(handle, b(args.platform), b(args.id1), b(args.id2), b(args.id3), b(args.id4))
    if hasattr(args, "handshake"):
        gc.lib.gc_set_handshake(handle, b(args.handshake))
    if getattr(args, "cert_pem", None):
        gc.lib.gc_set_encryption_certificate_pem(handle, b(args.cert_pem))
    if getattr(args, "cert_file", None):
        if not gc.lib.gc_set_encryption_certificate_file(handle, b(args.cert_file)):
            raise RuntimeError(text(gc.lib.gc_get_last_error(handle)))

    @gc.keep(gc.CB_CONNECTED)
    def on_connected(_ud):
        print("connected")

    @gc.keep(gc.CB_DISCONNECTED)
    def on_disconnected(reason, _ud):
        err = text(gc.lib.gc_get_last_error(handle))
        suffix = f" | last_error: {err}" if err else ""
        print("disconnected:", text(reason) + suffix)

    @gc.keep(gc.CB_AUTHENTICATED)
    def on_authenticated(_ud):
        nonlocal startup_sent
        print("authenticated")
        if startup_sent:
            return
        startup_sent = True
        if args.nick:
            if gc.lib.gc_set_nickname(handle, b(args.nick)):
                print(f"sent nickname: {args.nick}")
            else:
                print(f"nickname failed: {text(gc.lib.gc_get_last_error(handle))}")
        if args.chat:
            if gc.lib.gc_send_level_chat(handle, b(args.chat)):
                print(f"sent chat: {args.chat}")
            else:
                print(f"chat failed: {text(gc.lib.gc_get_last_error(handle))}")
        if getattr(args, "level_chat", ""):
            if gc.lib.gc_send_level_chat(handle, b(args.level_chat)):
                print(f"sent level chat: {args.level_chat}")
            else:
                print(f"level chat failed: {text(gc.lib.gc_get_last_error(handle))}")
        if getattr(args, "toall_chat", ""):
            if gc.lib.gc_send_toall_chat(handle, b(args.toall_chat)):
                print(f"sent toall chat: {args.toall_chat}")
            else:
                print(f"toall chat failed: {text(gc.lib.gc_get_last_error(handle))}")
        for filename in getattr(args, "request_file", []) or []:
            if gc.lib.gc_request_file(handle, b(filename)):
                print(f"requested file: {filename}")
            else:
                print(f"request file failed: {filename}: {text(gc.lib.gc_get_last_error(handle))}")
        if getattr(args, "warp_level", ""):
            try:
                level, x, y = parse_warp_arg(args.warp_level)
            except ValueError as exc:
                print(f"level warp failed: {exc}")
            else:
                if gc.lib.gc_level_warp(handle, x, y, b(level)):
                    print(f"sent level warp: {level} ({x},{y})")
                else:
                    print(f"level warp failed: {text(gc.lib.gc_get_last_error(handle))}")
        level_board = getattr(args, "request_level_board", None)
        if level_board:
            request_level_board(level_board)

    def request_level_board(level_name=""):
        nonlocal level_board_requested
        if level_board_requested:
            return
        level_board_requested = True
        if gc.lib.gc_request_level_board(handle, b(level_name), 0, 0, 0, 64, 64):
            print(f"requested level board: {level_name if level_name else 'current level'}")
        else:
            print(f"request level board failed: {text(gc.lib.gc_get_last_error(handle))}")

    def send_pending_move(start_x=None, start_y=None):
        nonlocal move_sent
        if move_sent or not getattr(args, "move", ""):
            return
        try:
            x, y, direction, precise = parse_move_arg(args.move)
        except ValueError as exc:
            print(f"move failed: {exc}")
            move_sent = True
            return
        move_sent = True

        def send_one(px, py, label):
            if gc.lib.gc_move(handle, px, py, direction, precise):
                print(f"sent {label}: x={px:.3f} y={py:.3f} dir={direction} precise={precise}")
                return True
            print(f"move failed: {text(gc.lib.gc_get_last_error(handle))}")
            return False

        def run_walk():
            if start_x is None or start_y is None:
                send_one(x, y, "move")
                return
            dx = x - start_x
            dy = y - start_y
            distance = math.hypot(dx, dy)
            if distance <= 0.5:
                send_one(x, y, "move")
                return
            steps = max(1, int(math.ceil(distance / 0.5)))
            for index in range(1, steps + 1):
                px = start_x + (dx * index / steps)
                py = start_y + (dy * index / steps)
                if not send_one(px, py, f"walk step {index}/{steps}"):
                    break
                time.sleep(0.125)

        threading.Thread(target=run_walk, daemon=True).start()

    def direction_for_step(dx, dy):
        if abs(dx) >= abs(dy):
            return 3 if dx > 0 else 1
        return 2 if dy > 0 else 0

    def send_pending_walk(start_x=None, start_y=None):
        nonlocal walk_sent
        if walk_sent or not getattr(args, "walk", ""):
            return
        if start_x is None or start_y is None:
            return
        parts = [part.strip() for part in args.walk.split(",")]
        if len(parts) < 2:
            print("walk failed: expected x,y[,step,delay,precise,gani]")
            walk_sent = True
            return
        target_x = float(parts[0])
        target_y = float(parts[1])
        step_size = float(parts[2]) if len(parts) > 2 and parts[2] else 0.5
        delay = float(parts[3]) if len(parts) > 3 and parts[3] else 0.125
        precise = int(parts[4]) if len(parts) > 4 and parts[4] else 1
        gani = parts[5] if len(parts) > 5 and parts[5] else "walk"
        walk_sent = True

        def send_one(px, py, direction, label):
            if gc.lib.gc_walk_step(handle, px, py, direction, b(gani), precise):
                print(f"sent {label}: x={px:.3f} y={py:.3f} dir={direction} gani={gani} precise={precise}")
                return True
            print(f"walk failed: {text(gc.lib.gc_get_last_error(handle))}")
            return False

        def run_walk():
            dx = target_x - start_x
            dy = target_y - start_y
            distance = math.hypot(dx, dy)
            if distance <= 0.01:
                return
            steps = max(1, int(math.ceil(distance / max(0.01, step_size))))
            for index in range(1, steps + 1):
                prev_x = start_x + (dx * (index - 1) / steps)
                prev_y = start_y + (dy * (index - 1) / steps)
                px = start_x + (dx * index / steps)
                py = start_y + (dy * index / steps)
                direction = direction_for_step(px - prev_x, py - prev_y)
                if not send_one(px, py, direction, f"walk step {index}/{steps}"):
                    break
                time.sleep(delay)

        threading.Thread(target=run_walk, daemon=True).start()

    def props_summary(decoded):
        parts = []
        if "level" in decoded:
            parts.append(f"level={decoded['level']}")
        x = decoded.get("precise_x", decoded.get("x"))
        y = decoded.get("precise_y", decoded.get("y"))
        if x is not None or y is not None:
            parts.append(f"pos={x},{y}")
        if "gmap_level_x" in decoded or "gmap_level_y" in decoded:
            parts.append(f"gmap={decoded.get('gmap_level_x')},{decoded.get('gmap_level_y')}")
        return f" ({'; '.join(parts)})" if parts else ""

    @gc.keep(gc.CB_RAW)
    def on_raw(packet_id, _data, length, _ud):
        print(f"packet {packet_id} ({length} bytes)")

    @gc.keep(gc.CB_PACKET_EVENT)
    def on_packet_event(packet_id, packet_name, event_json, _ud):
        print(f"packet-event[{packet_id} {text(packet_name)}]: {text(event_json)}")

    @gc.keep(gc.CB_CHAT)
    def on_chat(player_id, message, _ud):
        print(f"chat[{player_id}]: {text(message)}")

    @gc.keep(gc.CB_CHAT_EX)
    def on_chat_ex(player_id, account, nickname, community, level, message, _ud):
        print(
            "chat-ex[player_id={0} account={1} nick={2} community={3} level={4}]: {5}".format(
                player_id,
                text(account),
                text(nickname),
                text(community),
                text(level),
                text(message),
            )
        )

    @gc.keep(gc.CB_PRIVATE_MESSAGE)
    def on_private_message(player_id, message_type, message, _ud):
        player = take_string(gc, gc.lib.gc_get_player_json(handle, player_id))
        try:
            decoded = json.loads(player) if player else {}
        except json.JSONDecodeError:
            decoded = {}
        print(
            "private-message[player_id={0} account={1} nick={2} type={3}]: {4}".format(
                player_id,
                decoded.get("account", ""),
                decoded.get("nickname", ""),
                text(message_type),
                text(message),
            )
        )

    @gc.keep(gc.CB_LEVEL_NAME)
    def on_level_name(level, _ud):
        name = text(level)
        print(f"level-name: {name}")
        if getattr(args, "request_level_board", None) == "":
            request_level_board("")

    @gc.keep(gc.CB_PLAYER_PROPS)
    def on_player_props(props_json, _ud):
        nonlocal self_player_id
        props = text(props_json)
        try:
            decoded = json.loads(props)
        except json.JSONDecodeError:
            decoded = {}
        if "id" in decoded:
            self_player_id = decoded["id"]
        self_label = f"self id={self_player_id}" if self_player_id is not None else "self id=?"
        print(f"player-props[{self_label}]{props_summary(decoded)}: {props}")
        if getattr(args, "show_cache", False) and self_player_id is not None:
            cached = take_string(gc, gc.lib.gc_get_self_player_json(handle))
            if cached and cached != "{}":
                print(f"player-cache[self id={self_player_id}]: {cached}")
        if getattr(args, "request_level_board", None) == "" and "level" in decoded:
            request_level_board("")
        if getattr(args, "move", ""):
            if any(key in decoded for key in ("precise_x", "precise_y", "x", "y", "level")):
                start_x = decoded.get("precise_x", decoded.get("x"))
                start_y = decoded.get("precise_y", decoded.get("y"))
                send_pending_move(start_x, start_y)
        if getattr(args, "walk", ""):
            if any(key in decoded for key in ("precise_x", "precise_y", "x", "y", "level")):
                start_x = decoded.get("precise_x", decoded.get("x"))
                start_y = decoded.get("precise_y", decoded.get("y"))
                send_pending_walk(start_x, start_y)

    @gc.keep(gc.CB_OTHER_PLAYER_PROPS)
    def on_other_player_props(player_id, props_json, _ud):
        props = text(props_json)
        try:
            decoded = json.loads(props)
        except json.JSONDecodeError:
            decoded = {}
        print(f"other-player-props[player_id={player_id}]{props_summary(decoded)}: {props}")
        if getattr(args, "show_cache", False):
            cached = take_string(gc, gc.lib.gc_get_player_json(handle, player_id))
            if cached and cached != "{}":
                print(f"player-cache[player_id={player_id}]: {cached}")

    @gc.keep(gc.CB_SERVER_TEXT)
    def on_server_text(value, _ud):
        print(f"server-text: {text(value)}")

    @gc.keep(gc.CB_FLAG_SET)
    def on_flag_set(name, value, _ud):
        print(f"flag-set: {text(name)}={text(value)}")

    @gc.keep(gc.CB_FLAG_DEL)
    def on_flag_del(name, _ud):
        print(f"flag-del: {text(name)}")

    @gc.keep(gc.CB_WEAPON_SCRIPT)
    def on_weapon_script(script_type, script_name, _data, length, _ud):
        print(f"weapon-script: {text(script_type)},{text(script_name)} ({length} bytes)")

    @gc.keep(gc.CB_RESOURCE)
    def on_resource(resource_type, name, _data, length, packet_id, _ud):
        dump_types = {part.strip() for part in getattr(args, "dump_resource_types", "").split(",") if part.strip()}
        if dump_types and text(resource_type) not in dump_types:
            return
        print(f"resource[{text(resource_type)} packet={packet_id}]: {text(name)} ({length} bytes)")

    gc.lib.gc_on_connected(handle, on_connected, None)
    gc.lib.gc_on_disconnected(handle, on_disconnected, None)
    gc.lib.gc_on_authenticated(handle, on_authenticated, None)
    gc.lib.gc_on_raw_packet(handle, on_raw, None)
    gc.lib.gc_on_packet_event(handle, on_packet_event, None)
    gc.lib.gc_on_chat(handle, on_chat, None)
    gc.lib.gc_on_chat_ex(handle, on_chat_ex, None)
    gc.lib.gc_on_private_message(handle, on_private_message, None)
    gc.lib.gc_on_level_name(handle, on_level_name, None)
    gc.lib.gc_on_player_props(handle, on_player_props, None)
    gc.lib.gc_on_other_player_props(handle, on_other_player_props, None)
    gc.lib.gc_on_server_text(handle, on_server_text, None)
    gc.lib.gc_on_flag_set(handle, on_flag_set, None)
    gc.lib.gc_on_flag_del(handle, on_flag_del, None)
    gc.lib.gc_on_weapon_script(handle, on_weapon_script, None)
    gc.lib.gc_on_resource(handle, on_resource, None)

    try:
        if not gc.lib.gc_connect(handle):
            raise RuntimeError(text(gc.lib.gc_get_last_error(handle)))
        if not gc.lib.gc_login(handle, b(args.account), b(args.password)):
            raise RuntimeError(text(gc.lib.gc_get_last_error(handle)))

        deadline = time.time() + args.seconds
        while time.time() < deadline and gc.lib.gc_is_connected(handle):
            time.sleep(0.1)
    finally:
        gc.lib.gc_disconnect(handle)
        gc.lib.gc_destroy(handle)
