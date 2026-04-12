#!/usr/bin/env python3
"""
RPiPacBot UDP test server.

Listens for packets from the Pi, decodes them according to PROTOCOL.md,
and provides an interactive command prompt to send commands back.

Usage:
    python3 test_server.py [--port 9000]
"""

import argparse
import json
import os
import socket
import struct
import threading
import time
import tkinter as tk
import sys

# -- Message types ------------------------------------------------------
MSG_ALIVE       = 0x01
MSG_DEVICE_INFO = 0x02
MSG_SENSOR_DATA = 0x03
MSG_LOG         = 0x04
MSG_PONG        = 0x05

CMD_START_LOG       = 0x11
CMD_SET_MOTOR       = 0x12
CMD_SET_MOTORS      = 0x15
CMD_SET_INTERVAL    = 0x13
CMD_PING            = 0x14
CMD_STATUS          = 0x16
CMD_SET_DRIVE_MODE  = 0x17
CMD_CARDINAL_MOVE   = 0x18
CMD_CALIBRATE       = 0x19

# Drive modes
DRIVE_MANUAL         = 0
DRIVE_CARDINAL_LOCKED = 1

# Cardinal directions
DIR_STOP  = 0
DIR_NORTH = 1
DIR_EAST  = 2
DIR_SOUTH = 3
DIR_WEST  = 4

DIR_NAMES = {"stop": DIR_STOP, "north": DIR_NORTH, "east": DIR_EAST,
             "south": DIR_SOUTH, "west": DIR_WEST,
             "n": DIR_NORTH, "e": DIR_EAST, "s": DIR_SOUTH, "w": DIR_WEST}

LOG_LEVELS = {0: "DEBUG", 1: "INFO", 2: "WARN", 3: "ERROR"}

CONFIG_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                           "motor_config.json")


def load_motor_config(path: str = CONFIG_PATH) -> dict:
    """Load motor direction multipliers from JSON config."""
    with open(path) as f:
        cfg = json.load(f)
    required = {"forward", "backward", "left", "right", "rotate_ccw", "rotate_cw"}
    missing = required - cfg.keys()
    if missing:
        raise ValueError(f"Missing keys in motor config: {missing}")
    for key in required:
        if len(cfg[key]) != 4:
            raise ValueError(f"'{key}' must have exactly 4 motor values")
    return cfg


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


class ControlGUI:
    """Tkinter GUI for directional motor control."""

    ACTIONS = [
        ("forward",    "W"),
        ("left",       "A"),
        ("backward",   "S"),
        ("right",      "D"),
        ("rotate_ccw", "Q"),
        ("rotate_cw",  "E"),
        ("stop",       "X"),
    ]

    # Labels per mode:  manual_label, cardinal_label
    MANUAL_LABELS = {
        "forward": "forward", "backward": "backward",
        "left": "left", "right": "right",
        "rotate_ccw": "rot CCW", "rotate_cw": "rot CW",
        "stop": "stop",
    }
    CARDINAL_LABELS = {
        "forward": "north", "backward": "south",
        "left": "west", "right": "east",
        "rotate_ccw": "", "rotate_cw": "",
        "stop": "stop",
    }
    # Map manual action -> cardinal direction
    ACTION_TO_DIR = {
        "forward": DIR_NORTH, "backward": DIR_SOUTH,
        "left": DIR_WEST, "right": DIR_EAST,
        "stop": DIR_STOP,
    }

    def __init__(self, server: "TestServer"):
        self.server = server
        self.config = load_motor_config()
        self.cardinal_mode = False

        self.root = tk.Tk()
        self.root.title("RPiPacBot Control")
        self.root.resizable(False, False)

        # --- Mode toggle ---
        mode_frame = tk.Frame(self.root)
        mode_frame.pack(pady=6)
        self.mode_btn = tk.Button(mode_frame, text="Mode: MANUAL",
                                  width=20, command=self._toggle_mode)
        self.mode_btn.pack(side=tk.LEFT, padx=4)
        tk.Button(mode_frame, text="Calibrate (set north)",
                  width=20, command=self._calibrate).pack(side=tk.LEFT, padx=4)

        # --- Speed slider ---
        speed_frame = tk.Frame(self.root)
        speed_frame.pack(pady=6)
        tk.Label(speed_frame, text="Speed:").pack(side=tk.LEFT)
        self.speed_var = tk.IntVar(value=128)
        self.speed_scale = tk.Scale(speed_frame, from_=0, to=255,
                                    orient=tk.HORIZONTAL, variable=self.speed_var,
                                    length=200)
        self.speed_scale.pack(side=tk.LEFT, padx=4)

        # --- Button grid ---
        # Layout:
        #   [Q rotate_ccw]  [W forward]   [E rotate_cw]
        #   [A left]        [X stop]      [D right]
        #                   [S backward]
        grid = tk.Frame(self.root)
        grid.pack(pady=6)

        positions = {
            "rotate_ccw": (0, 0), "forward":  (0, 1), "rotate_cw": (0, 2),
            "left":       (1, 0), "stop":     (1, 1), "right":     (1, 2),
                                  "backward": (2, 1),
        }
        self.buttons = {}
        for action, key in self.ACTIONS:
            r, c = positions[action]
            label = f"{action}\n[{key}]"
            btn = tk.Button(grid, text=label, width=12, height=2,
                            command=lambda a=action: self._on_action(a))
            btn.grid(row=r, column=c, padx=2, pady=2)
            self.buttons[action] = btn

        # --- Reload config button ---
        tk.Button(self.root, text="Reload config", command=self._reload_config
                  ).pack(pady=6)

        # --- Status bar ---
        self.status_var = tk.StringVar(value="Ready  [Mode: MANUAL]")
        tk.Label(self.root, textvariable=self.status_var, anchor=tk.W,
                 relief=tk.SUNKEN).pack(fill=tk.X, side=tk.BOTTOM)

        # --- Key bindings ---
        key_map = {k.lower(): a for a, k in self.ACTIONS}
        self.root.bind("<KeyPress>", lambda e: self._on_action(key_map[e.char])
                       if e.char in key_map else None)

    def _calibrate(self):
        self.server.send_cmd(bytes([CMD_CALIBRATE]))
        self.status_var.set("Calibrated (current heading = north)")

    def _toggle_mode(self):
        self.cardinal_mode = not self.cardinal_mode
        if self.cardinal_mode:
            mode_byte = DRIVE_CARDINAL_LOCKED
            self.mode_btn.config(text="Mode: CARDINAL LOCKED")
            labels = self.CARDINAL_LABELS
        else:
            mode_byte = DRIVE_MANUAL
            self.mode_btn.config(text="Mode: MANUAL")
            labels = self.MANUAL_LABELS

        # Send mode switch to robot
        self.server.send_cmd(struct.pack("<BB", CMD_SET_DRIVE_MODE, mode_byte))

        # Relabel buttons
        for action, key in self.ACTIONS:
            lbl = labels[action]
            if lbl:
                self.buttons[action].config(text=f"{lbl}\n[{key}]",
                                            state=tk.NORMAL)
            else:
                self.buttons[action].config(text=f"---\n[{key}]",
                                            state=tk.DISABLED)

        mode_name = "CARDINAL" if self.cardinal_mode else "MANUAL"
        self.status_var.set(f"Switched to {mode_name}")

    def _on_action(self, action: str):
        speed = self.speed_var.get()

        if self.cardinal_mode:
            d = self.ACTION_TO_DIR.get(action)
            if d is None:
                return  # rotation keys ignored in cardinal mode
            if d == DIR_STOP:
                speed = 0
            payload = struct.pack("<BBB", CMD_CARDINAL_MOVE, d, speed)
            self.server.send_cmd(payload)
            name = {DIR_NORTH: "north", DIR_EAST: "east",
                    DIR_SOUTH: "south", DIR_WEST: "west",
                    DIR_STOP: "stop"}.get(d, "?")
            self.status_var.set(f"{name} speed={speed}")
        else:
            if action == "stop":
                speeds = [0, 0, 0, 0]
            else:
                mults = self.config[action]
                speeds = [max(-255, min(255, int(m * speed))) for m in mults]
            payload = struct.pack("<B4h", CMD_SET_MOTORS, *speeds)
            self.server.send_cmd(payload)
            self.status_var.set(f"{action} -> motors {speeds}")

    def _reload_config(self):
        try:
            self.config = load_motor_config()
            self.status_var.set("Config reloaded")
        except Exception as exc:
            self.status_var.set(f"Config error: {exc}")

    def run(self):
        self.root.mainloop()


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
        print("  log               Send CMD_START_LOG")
        print("  status            Request device info (connected sensors/IMU)")
        print("  mode manual|cardinal  Switch drive mode")
        print("  calibrate         Set current heading as north")
        print("  move DIR [SPEED]  Cardinal move (north/east/south/west/stop, default speed 128)")
        print("  motor INDEX SPEED  Set one motor (-255..255, sign=direction)")
        print("  motors S0 S1 S2 S3  Set all motors (-255..255 each)")
        print("  interval N        Set send interval (ms)")
        print("  ping              Send PING")
        print("  gui               Open motor control GUI")
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

                elif cmd == "mode":
                    if len(parts) != 2 or parts[1] not in ("manual", "cardinal"):
                        print("  Usage: mode manual|cardinal")
                        continue
                    m = DRIVE_MANUAL if parts[1] == "manual" else DRIVE_CARDINAL_LOCKED
                    self.send_cmd(struct.pack("<BB", CMD_SET_DRIVE_MODE, m))
                    print(f"  -> CMD_SET_DRIVE_MODE {parts[1]} sent")

                elif cmd == "calibrate":
                    self.send_cmd(bytes([CMD_CALIBRATE]))
                    print("  -> CMD_CALIBRATE sent (current heading = north)")

                elif cmd == "move":
                    if len(parts) < 2:
                        print("  Usage: move <dir> [speed]  (north/east/south/west/stop)")
                        continue
                    d = DIR_NAMES.get(parts[1])
                    if d is None:
                        print(f"  Unknown direction '{parts[1]}' "
                              f"(use: {', '.join(DIR_NAMES.keys())})")
                        continue
                    speed = 128
                    if len(parts) >= 3:
                        try:
                            speed = max(0, min(255, int(parts[2])))
                        except ValueError:
                            print("  Speed must be an integer 0-255")
                            continue
                    self.send_cmd(struct.pack("<BBB", CMD_CARDINAL_MOVE, d, speed))
                    print(f"  -> CMD_CARDINAL_MOVE dir={parts[1]} speed={speed} sent")

                elif cmd == "status":
                    self.send_cmd(bytes([CMD_STATUS]))
                    print("  -> CMD_STATUS sent")

                elif cmd == "ping":
                    self.send_cmd(bytes([CMD_PING]))
                    t = time.monotonic()
                    print(f"  -> CMD_PING sent at {t:.3f}")

                elif cmd == "gui":
                    print("  Opening control GUI...")
                    gui = ControlGUI(self)
                    gui.run()
                    print("  GUI closed")

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
