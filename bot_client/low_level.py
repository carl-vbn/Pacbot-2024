from gameState import Directions
import socket
import time

s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)

connected = False

def connect():
    global connected
    try:
        s.connect("/tmp/pacbot.sock")
        connected = True
    except socket.error as e:
        print(f"[Low level] Could not connect to robot socket: {e}")
        connected = False

chars = {
    Directions.UP: 'n',
    Directions.DOWN: 's',
    Directions.RIGHT: 'e',
    Directions.LEFT: 'w',
    Directions.NONE: 'x'
}

def send_direction(direction: Directions) -> None:
    '''
    Send a movement direction to the low-level motor controller.
    Called only when the desired direction changes.
    '''
    if not connected:
        print("[Low level] Not connected to robot socket.")
        return

    try:
        s.sendall(f"{chars[direction]}\n".encode())
    except socket.error as e:
        print(f"[Low level] Could not send direction to robot socket: {e}")

def unstuck():
    '''
    Send all four directions in quick succession to try to free the robot if it's stuck.
    '''

    for direction in [Directions.UP, Directions.DOWN, Directions.RIGHT, Directions.LEFT, Directions.NONE]:
        send_direction(direction)
        time.sleep(0.2)

connect()
