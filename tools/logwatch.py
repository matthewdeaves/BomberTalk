#!/usr/bin/env python3
"""
logwatch.py -- Listen for BomberTalk UDP broadcast logs

Listens on port 7355 for UDP broadcast log messages from all
Classic Mac machines running BomberTalk. Each log line is prefixed
with the sender's IP address.

Usage:
    python3 tools/logwatch.py
    python3 tools/logwatch.py --port 7355
"""

import socket
import sys
import argparse
from datetime import datetime


def main():
    parser = argparse.ArgumentParser(description="BomberTalk log listener")
    parser.add_argument("--port", type=int, default=7355,
                        help="UDP port to listen on (default: 7355)")
    args = parser.parse_args()

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    sock.bind(("", args.port))

    print(f"Listening for BomberTalk logs on UDP port {args.port}...")
    print(f"{'='*60}")

    try:
        while True:
            data, addr = sock.recvfrom(1024)
            ip = addr[0]
            try:
                msg = data.decode("ascii", errors="replace").rstrip()
            except Exception:
                msg = repr(data)
            ts = datetime.now().strftime("%H:%M:%S.%f")[:-3]
            print(f"[{ts}] {ip:>15s} | {msg}")
    except KeyboardInterrupt:
        print("\nStopped.")
    finally:
        sock.close()


if __name__ == "__main__":
    main()
