from gclib_common import GC_VERSION_62, make_parser, run_basic_client


if __name__ == "__main__":
    parser = make_parser("Connect/login through GCLib using the Gen6 client protocol.", "6.2")
    parser.add_argument("--handshake", default="GNP1905C")
    args = parser.parse_args()
    run_basic_client(args, GC_VERSION_62)
