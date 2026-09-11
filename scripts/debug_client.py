#!/usr/bin/env python3
"""Local GAME/1 client. Standard library only; this is not the old game client."""

import argparse
import socket
import struct
import sys
import threading

MAX_MESSAGE_BYTES = 4096


def port_number(text):
    if not text.isascii() or not text.isdecimal() or not 1 <= int(text) <= 65535:
        raise argparse.ArgumentTypeError("port must be in 1..65535")
    return int(text)


def read_exact(connection, count):
    chunks = bytearray()
    while len(chunks) < count:
        part = connection.recv(count - len(chunks))
        if not part:
            raise EOFError("server closed the connection")
        chunks.extend(part)
    return bytes(chunks)


def receive(connection):
    length = struct.unpack(">I", read_exact(connection, 4))[0]
    if not 0 < length <= MAX_MESSAGE_BYTES:
        raise ValueError("server frame exceeds protocol bounds")
    return read_exact(connection, length).decode("ascii")


def send(connection, command):
    if not command.startswith("GAME/"):
        command = "GAME/1 " + command
    payload = command.encode("ascii")
    if not 0 < len(payload) <= MAX_MESSAGE_BYTES or any(value < 32 or value > 126 for value in payload):
        raise ValueError("commands must be one ASCII line, at most 4096 bytes")
    connection.sendall(struct.pack(">I", len(payload)) + payload)


def receive_reply(connection):
    while True:
        message = receive(connection)
        print(message, flush=True)
        if not message.startswith("GAME/1 EVENTS"):
            return message


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=port_number, default=7777)
    parser.add_argument("--command", action="append", help="send command; repeat for a scripted session")
    args = parser.parse_args()
    with socket.create_connection(("127.0.0.1", args.port), timeout=5) as connection:
        print(receive(connection), flush=True)
        if args.command:
            failed = False
            for command in args.command:
                send(connection, command)
                if command in ("QUIT", "GAME/1 QUIT"):
                    break  # Disconnect may discard the queued BYE acknowledgment.
                failed |= receive_reply(connection).startswith("GAME/1 ERROR")
            return 1 if failed else 0

        connection.settimeout(None)
        stopped = threading.Event()

        def reader():
            try:
                while not stopped.is_set():
                    print("\n" + receive(connection), flush=True)
            except (EOFError, OSError, ValueError, UnicodeError) as error:
                if not stopped.is_set():
                    print("\n" + str(error), file=sys.stderr, flush=True)
            finally:
                stopped.set()

        worker = threading.Thread(target=reader, daemon=True)
        worker.start()
        try:
            while not stopped.is_set():
                try:
                    command = input("game> ").strip()
                except EOFError:
                    break
                if not command:
                    continue
                try:
                    send(connection, command)
                except (ValueError, UnicodeError) as error:
                    print(error, file=sys.stderr)
                    continue
                if command in ("QUIT", "GAME/1 QUIT"):
                    break
        finally:
            stopped.set()
            try:
                connection.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
            worker.join(timeout=1)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (EOFError, OSError, ValueError, UnicodeError) as error:
        print("debug_client:", error, file=sys.stderr)
        raise SystemExit(1)
    except KeyboardInterrupt:
        raise SystemExit(130)
