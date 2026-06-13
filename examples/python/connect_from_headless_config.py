import argparse
import socket
import os
from pathlib import Path

from gclib_common import GC_VERSION_62, run_basic_client


DEFAULT_SETTINGS = Path(os.environ.get("GCLIB_HEADLESS_SETTINGS", "settings.ini"))


def read_headless_settings(path):
    path = Path(path)
    if not path.exists():
        raise SystemExit(f"Settings file not found: {path}. Copy examples/python/settings.sample.ini to settings.ini or pass --settings.")
    values = {}
    lines = path.read_text(errors="replace").splitlines()
    i = 0
    while i < len(lines):
        line = lines[i].strip()
        i += 1
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        key = key.strip()
        value = value.strip()
        if key == "security.private_key_server_to_client" and "-----BEGIN PRIVATE KEY-----" in value:
            pem = [value]
            while i < len(lines):
                pem.append(lines[i].rstrip())
                if "-----END PRIVATE KEY-----" in lines[i]:
                    i += 1
                    break
                i += 1
            value = "\n".join(pem)
        values[key] = value
    return values


def java_string_hash(value):
    h = 0
    for ch in value:
        h = (31 * h + ord(ch)) & 0xFFFFFFFF
    return h - 0x100000000 if h & 0x80000000 else h


def computer_code(computer_hash):
    hex_code = "0123456789ABCDEF"
    result = []
    h = computer_hash
    for _ in range(32):
        result.append(hex_code[abs(h % 16)])
        h = ((h * 31 + 0x80000000) & 0xFFFFFFFF) - 0x80000000
    return "".join(result)


def local_ip_hash():
    try:
        ip = socket.gethostbyname(socket.gethostname())
        raw = bytes(int(part) for part in ip.split("."))
        value = int.from_bytes(raw, "big", signed=False)
        return value - 0x100000000 if value & 0x80000000 else value
    except Exception:
        return 0


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Connect with GCLib using a settings.ini-style server and V6 cert configuration.")
    parser.add_argument("--settings", default=str(DEFAULT_SETTINGS))
    parser.add_argument("--host", default="")
    parser.add_argument("--port", default=0, type=int)
    parser.add_argument("--account", default=os.environ.get("GCLIB_TEST_ACCOUNT", ""))
    parser.add_argument("--password", default=os.environ.get("GCLIB_TEST_PASSWORD", ""))
    parser.add_argument("--nick", default="GCLibV6")
    parser.add_argument("--chat", default="")
    parser.add_argument("--level-chat", default="")
    parser.add_argument("--toall-chat", default="")
    parser.add_argument("--warp-level", default="", help="Warp as level,x,y before optional board request")
    parser.add_argument("--move", default="")
    parser.add_argument("--walk", default="")
    parser.add_argument("--show-cache", action="store_true")
    parser.add_argument("--dump-resources", default="")
    parser.add_argument("--dump-resource-types", default="")
    parser.add_argument("--request-file", action="append", default=[])
    parser.add_argument("--request-level-board", nargs="?", const="", default=None)
    parser.add_argument("--seconds", default=20, type=int)
    args = parser.parse_args()

    settings = read_headless_settings(args.settings)
    if not args.account:
        raise SystemExit("Provide --account or set GCLIB_TEST_ACCOUNT.")
    if not args.password:
        raise SystemExit("Provide --password or set GCLIB_TEST_PASSWORD.")

    class RunArgs:
        pass

    run = RunArgs()
    run.host = args.host or settings.get("server.ip", "127.0.0.1")
    run.port = args.port or int(settings.get("server.port", "14720"))
    run.account = args.account
    run.password = args.password
    run.platform = "win"
    run.id1 = ""
    run.id2 = ""
    run.id3 = ""
    run.id4 = ""
    configured_pcids = settings.get("login.pcidhashes", "")
    if configured_pcids:
        pcid = computer_code(java_string_hash(configured_pcids.split(",")[0]))
    else:
        pcid = computer_code(local_ip_hash() + java_string_hash(args.account))
    run.client_info = f"win,{pcid},{pcid},{pcid},{pcid}"
    run.nick = args.nick
    run.chat = args.chat
    run.level_chat = args.level_chat
    run.toall_chat = args.toall_chat
    run.warp_level = args.warp_level
    run.move = args.move
    run.walk = args.walk
    run.show_cache = args.show_cache
    run.dump_resources = args.dump_resources
    run.dump_resource_types = args.dump_resource_types
    run.request_file = args.request_file
    run.request_level_board = args.request_level_board
    run.seconds = args.seconds
    run.handshake = "GNP1905C"
    run.cert_pem = settings.get("security.private_key_server_to_client", "")
    run.cert_file = ""

    print(f"connecting to {run.host}:{run.port} as {run.account}")
    run_basic_client(run, GC_VERSION_62)
