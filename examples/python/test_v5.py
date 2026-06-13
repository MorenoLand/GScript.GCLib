from gclib_common import GC_VERSION_6037, make_parser, run_basic_client


if __name__ == "__main__":
    parser = make_parser("Connect/login through GCLib using the Gen5 client protocol.", "6.037")
    run_basic_client(parser.parse_args(), GC_VERSION_6037)
