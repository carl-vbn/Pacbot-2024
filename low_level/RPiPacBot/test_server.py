#!/usr/bin/env python3
"""
RPiPacBot UDP test server.

Listens for packets from the Pi, decodes them according to PROTOCOL.md,
and provides an interactive command prompt to send commands back.

Usage:
    python3 test_server.py [--port 9000]
"""

import argparse
import socket
import struct
import threading
import time
import sys

# -- Message types ------------------------------------------------------
MSG_ALIVE       = 0x01
MSG_DEVICE_INFO = 0x02
MSG_SENSOR_DATA = 0x03
MSG_LOG         = 0x04
MSG_PONG        = 0x05

CMD_SETUP        = 0x10
CMD_START_LOG    = 0x11
CMD_SET_MOTOR    = 0x12
CMD_SET_MOTORS   = 0x15
CMD_SET_INTERVAL = 0x13
CMD_PING         = 0x14

LOG_LEVELS = {0: "DEBUG", 1: "INFO", 2: "WARN", 3: "ERROR"}


def decode_packet(data: bytes) -> str:
    """Decode a binary packet into a human-readable string."""
    if len(data) < 1:
        return "[empty packet]"

    msg_type = data[0]

    if msg_type == MSG_ALIVE:
        if len(data) < 5:
            return f"[ALIVE] incomplete ({len(data)} bytes)"
        uptime = struct.unpack_from("<I", data, 1)[0]
        return f"[ALIVE] uptime={uptime} ms"

    elif msg_type == MSG_DEVICE_INFO:
        if len(data) < 4:
            return f"[DEVICE_INFO] incomplete ({len(data)} bytes)"
        mask, imu, motors = data[1], data[2], data[3]
        slots = [i for i in range(8) if mask & (1 << i)]
        return (f"[DEVICE_INFO] sensors={slots} imu={'yes' if imu else 'no'} "
                f"motors={motors}")

    elif msg_type == MSG_SENSOR_DATA:
        if len(data) < 6:
            return f"[SENSOR_DATA] incomplete ({len(data)} bytes)"
        ts = struct.unpack_from("<I", data, 1)[0]
        count = data[5]
        pos = 6
        readings = []
        for _ in range(count):
            if pos + 2 > len(data):
                break
            val = struct.unpack_from("<h", data, pos)[0]
            readings.append(str(val) if val >= 0 else "ERR")
            pos += 2
        imu_str = ""
        if pos < len(data) and data[pos]:
            pos += 1
            if pos + 12 <= len(data):
                yaw, pitch, roll = struct.unpack_from("<fff", data, pos)
                imu_str = f" imu=({yaw:.1f}, {pitch:.1f}, {roll:.1f})"
        else:
            pos += 1  # skip imu_flag=0
        return f"[SENSOR_DATA] t={ts} dist={readings}{imu_str}"

    elif msg_type == MSG_LOG:
        if len(data) < 4:
            return f"[LOG] incomplete ({len(data)} bytes)"
        severity = data[1]
        text_len = struct.unpack_from("<H", data, 2)[0]
        text = data[4:4 + text_len].decode("utf-8", errors="replace")
        level = LOG_LEVELS.get(severity, f"?{severity}")
        return f"[LOG/{level}] {text}"

    elif msg_type == MSG_PONG:
        if len(data) < 5:
            return f"[PONG] incomplete ({len(data)} bytes)"
        uptime = struct.unpack_from("<I", data, 1)[0]
        return f"[PONG] uptime={uptime} ms"

    else:
        return f"[UNKNOWN 0x{msg_type:02x}] {data.hex()}"


class TestServer:
    def __init__(self, listen_port: int):
        self.listen_port = listen_port
        self.pi_addr = None  # learned from first packet (ip, port)
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind(("0.0.0.0", listen_port))
        self.sock.settimeout(0.5)
        self.running = True

    def send_cmd(self, data: bytes):
        if self.pi_addr is None:
            print("  No Pi address known yet (waiting for first packet)")
            return
        self.sock.sendto(data, self.pi_addr)

    def receiver_loop(self):
        while self.running:
            try:
                data, addr = self.sock.recvfrom(512)
            except socket.timeout:
                continue
            except OSError:
                break

            if self.pi_addr != addr:
                label = "discovered" if self.pi_addr is None else "reconnected"
                self.pi_addr = addr
                print(f"\n  Pi {label} at {addr[0]}:{addr[1]}")

            if data[0] != MSG_ALIVE:
                msg = decode_packet(data)
                print(f"\n  <- {msg}")
                print("> ", end="", flush=True)

    def run(self):
        print(f"Listening on UDP port {self.listen_port}")
        print(f"Waiting for first Pi packet to learn its address...")
        print()
        print("Commands:")
        print("  setup             Send CMD_SETUP")
        print("  log               Send CMD_START_LOG")
        print("  motor INDEX SPEED  Set one motor (-255..255, sign=direction)")
        print("  motors S0 S1 S2 S3  Set all motors (-255..255 each)")
        print("  interval N        Set send interval (ms)")
        print("  ping              Send PING")
        print("  quit              Exit")
        print()

        rx_thread = threading.Thread(target=self.receiver_loop, daemon=True)
        rx_thread.start()

        try:
            while self.running:
                try:
                    line = input("> ").strip().lower()
                except EOFError:
                    break

                if not line:
                    continue

                parts = line.split()
                cmd = parts[0]

                if cmd == "quit" or cmd == "q":
                    break

                elif cmd == "setup":
                    self.send_cmd(bytes([CMD_SETUP]))
                    print("  -> CMD_SETUP sent")

                elif cmd == "log":
                    self.send_cmd(bytes([CMD_START_LOG]))
                    print("  -> CMD_START_LOG sent")

                elif cmd == "motor":
                    if len(parts) != 3:
                        print("  Usage: motor <index> <speed>  (-255..255)")
                        continue
                    try:
                        idx = int(parts[1])
                        speed = int(parts[2])
                    except ValueError:
                        print("  Index and speed must be integers")
                        continue
                    if idx < 0 or idx > 3:
                        print("  Index must be 0-3")
                        continue
                    speed = max(-255, min(255, speed))
                    payload = struct.pack("<Bbh", CMD_SET_MOTOR, idx, speed)
                    self.send_cmd(payload)
                    print(f"  -> CMD_SET_MOTOR idx={idx} speed={speed} sent")

                elif cmd == "motors":
                    if len(parts) != 5:
                        print("  Usage: motors S0 S1 S2 S3  (-255..255 each)")
                        continue
                    try:
                        speeds = [max(-255, min(255, int(x))) for x in parts[1:]]
                    except ValueError:
                        print("  All speeds must be integers")
                        continue
                    payload = struct.pack("<B4h", CMD_SET_MOTORS, *speeds)
                    self.send_cmd(payload)
                    print(f"  -> CMD_SET_MOTORS {speeds} sent")

                elif cmd == "interval":
                    if len(parts) != 2:
                        print("  Usage: interval <ms>")
                        continue
                    try:
                        ms = int(parts[1])
                    except ValueError:
                        print("  Interval must be an integer")
                        continue
                    payload = struct.pack("<BH", CMD_SET_INTERVAL, ms)
                    self.send_cmd(payload)
                    print(f"  -> CMD_SET_INTERVAL {ms} ms sent")

                elif cmd == "ping":
                    self.send_cmd(bytes([CMD_PING]))
                    t = time.monotonic()
                    print(f"  -> CMD_PING sent at {t:.3f}")

                else:
                    print(f"  Unknown command: {cmd}")

        except KeyboardInterrupt:
            pass

        self.running = False
        self.sock.close()
        print("\nServer stopped.")


def main():
    parser = argparse.ArgumentParser(description="RPiPacBot UDP test server")
    parser.add_argument("--port", type=int, default=9000,
                        help="Port to listen on (default: 9000)")
    args = parser.parse_args()

    server = TestServer(args.port)
    server.run()


if __name__ == "__main__":
    main()
