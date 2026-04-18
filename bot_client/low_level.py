from gameState import Directions
import socket
import asyncio

s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)

connected = False

def connect(force_no_bot: bool = False):
    global connected
    if force_no_bot:
        print("[Low level] force_no_bot: skipping robot socket connection")
        connected = False
        return
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

async def unstuck(state, stuck_pos: tuple) -> None:
    '''
    Send all four directions in quick succession to try to free the robot if it's stuck.
    Exits early if pacbot moves away from stuck_pos.
    '''
    print(f"UNSTUCK triggered at {stuck_pos}")
    for direction in [Directions.UP, Directions.DOWN, Directions.RIGHT, Directions.LEFT, Directions.NONE]:
        if (state.pacmanLoc.row, state.pacmanLoc.col) != stuck_pos:
            print("Pacbot moved, exiting unstuck")
            break
        send_direction(direction)
        await asyncio.sleep(0.2)

