#!/usr/bin/env python3
"""Example ZeroMQ client for tbs-receiver.

    pip install pyzmq
    python3 zmq_receiver.py --endpoint tcp://127.0.0.1:5555 --out capture.bin [--topic tbs] [--header]

Use the same topic / header settings as configured in the receiver's "Output" tab.
With --pull the script binds a PULL socket (receiver set to PUSH + Connect).
"""
import argparse
import struct
import sys

import zmq

HEADER = struct.Struct("<4sBBHIQ")  # magic, type, flags, reserved, sequence, timestamp_us


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--endpoint", default="tcp://127.0.0.1:5555")
    ap.add_argument("--topic", default="")
    ap.add_argument("--header", action="store_true", help="messages carry the 20-byte metadata header")
    ap.add_argument("--pull", action="store_true", help="bind a PULL socket instead of connecting a SUB")
    ap.add_argument("--out", help="write payloads to this file")
    args = ap.parse_args()

    ctx = zmq.Context()
    if args.pull:
        sock = ctx.socket(zmq.PULL)
        sock.bind(args.endpoint)
    else:
        sock = ctx.socket(zmq.SUB)
        sock.setsockopt(zmq.SUBSCRIBE, args.topic.encode())
        sock.connect(args.endpoint)

    out = open(args.out, "wb") if args.out else None
    count = 0
    last_seq = None
    try:
        while True:
            frames = sock.recv_multipart()
            payload = frames[-1]
            if args.header:
                magic, kind, _flags, _res, seq, ts_us = HEADER.unpack_from(payload)
                if magic != b"TBS1":
                    print("bad header", file=sys.stderr)
                    continue
                if last_seq is not None and seq != (last_seq + 1) & 0xFFFFFFFF:
                    print(f"gap: {last_seq} -> {seq}", file=sys.stderr)
                last_seq = seq
                payload = payload[HEADER.size:]
                if kind == 1 and count % 500 == 0:
                    matype = payload[0]
                    dfl = (payload[4] << 8) | payload[5]
                    print(f"BBFrame seq={seq} MATYPE=0x{matype:02x} DFL={dfl} bits")
            if out:
                out.write(payload)
            count += 1
            if count % 1000 == 0:
                print(f"{count} messages", file=sys.stderr)
    except KeyboardInterrupt:
        pass
    finally:
        if out:
            out.close()


if __name__ == "__main__":
    main()
