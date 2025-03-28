# JSON (for reading config.json)
import json

# Asyncio (for concurrency)
import asyncio

# Websockets (for communication with the server)
from websockets.sync.client import connect, ClientConnection # type: ignore
from websockets.exceptions import ConnectionClosedError # type: ignore
from websockets.typing import Data # type: ignore

# Game state
from gameState import GameState

# Decision module
from decisionModule import DecisionModule

# Server messages
from serverMessage import *

# Restore the ability to use Ctrl + C within asyncio
import signal
signal.signal(signal.SIGINT, signal.SIG_DFL)

# Terminal colors for formatting output text
from terminalColors import *

# Debug server
from debugServer import DebugServer

from pathfinding import find_path

#from RPi.GPIO import GPIO

# Argument parser for command-line arguments
import argparse

import sys

import numpy as np

from time import time

# Get the connect URL from the config.json file
def getConnectURL() -> str:

	# Read the configuration file
	with open('../config.json', 'r', encoding='UTF-8') as configFile:
		config = json.load(configFile)

	# Return the websocket connect address
	return f'ws://{config["ServerIP"]}:{config["WebSocketPort"]}'

class PacbotClient:
	'''
	Sample implementation of a websocket client to communicate with the
	Pacbot game server, using asyncio.
	'''

	def __init__(self, connectURL: str) -> None:
		'''
		Construct a new Pacbot client object
		'''

		# Connection URL (starts with ws://)
		self.connectURL: str = connectURL

		# Private variable to store whether the socket is open
		self._socketOpen: bool = False

		# Connection object to communicate with the server
		self.connection: ClientConnection

		# Game state object to store the game information
		self.state: GameState = GameState(False if args.games == 1 else True)

		# Decision module (policy) to make high-level decisions
		self.decisionModule: DecisionModule = DecisionModule(self.state, args.debug)
  
		# list of scores for each game
		self.scores = []
  
		# timestamp of last game over
		self.last_game_over_time = time()

	async def run(self) -> None:
		'''
		Connect to the server, then run
		'''

		# Connect to the websocket server
		await self.connect()

		try: # Try receiving messages indefinitely
			if self._socketOpen:
				await asyncio.gather(
					self.receiveLoop(),
					self.decisionModule.decisionLoop()
				)
		finally: # Disconnect once the connection is over
			await self.disconnect()

	async def connect(self) -> None:
		'''
		Connect to the websocket server
		'''

		# Connect to the specified URL
		try:
			self.connection = connect(self.connectURL)
			self._socketOpen = True
			self.state.setConnectionStatus(True)

		# If the connection is refused, log and return
		except ConnectionRefusedError:
			print(
				f'{RED}Websocket connection refused [{self.connectURL}]\n'
				f'Are the address and port correct, and is the '
				f'server running?{NORMAL}'
			)
			return

	async def disconnect(self) -> None:
		'''
		Disconnect from the websocket server
		'''

		# Close the connection
		if self._socketOpen:
			self.connection.close()
		self._socketOpen = False
		self.state.setConnectionStatus(False)

	# Return whether the connection is open
	def isOpen(self) -> bool:
		'''
		Check whether the connection is open (unused)
		'''
		return self._socketOpen

	async def receiveLoop(self) -> None:
		'''
		Receive loop for capturing messages from the server
		'''

		# Receive values as long as the connection is open
		while self.isOpen():

			# Try to receive messages (and skip to except in case of an error)
			try:

				# Receive a message from the connection
				message: Data = self.connection.recv()

				# Convert the message to bytes, if necessary
				messageBytes: bytes
				if isinstance(message, bytes):
					messageBytes = message # type: ignore
				else:
					messageBytes = message.encode('ascii') # type: ignore

				# Update the state, given this message from the server
				should_resume = self.state.update(messageBytes)

				if self.state.isGameOver() and (time() - self.last_game_over_time > 10):
					self.scores.append(self.state.currScore)
					self.last_game_over_time = time()
					curr_num_games = len(self.scores)
					if args.games == curr_num_games:
						print(f'{RED}Simulation finished!{NORMAL}')
						print(f'{GREEN}Scores: {self.scores}{NORMAL}' if len(self.scores) > 1 else f'{GREEN}Score: {self.scores[0]}{NORMAL}')
						if len(self.scores) > 1:
							print(f'{GREEN}Average score: {int(np.mean(self.scores))}{NORMAL}')
							print(f'{GREEN}Standard deviation: {int(np.std(self.scores))}{NORMAL}')
						if args.output:
							with open(args.output, 'w') as f:
								f.write(str(self.scores))
						await debug_server.reset_game()
						await debug_server.pause_game()
						await self.disconnect()
						sys.exit(0)
					else:
						if args.games >= 10 and (curr_num_games % (args.games // 10) == 0):
							print(f'{PINK}Simulation {int(curr_num_games/args.games*100)}% complete, avg score: {int(np.mean(self.scores))}, std dev: {int(np.std(self.scores))}{NORMAL}')
						if args.delay > 0:
							await asyncio.sleep(args.delay / 1000)
						self.state.currLives = 3
						self.state.currLevel = 1
						await debug_server.reset_game()
				elif should_resume:
					await debug_server.resume_game()

				# Write a response back to the server if necessary
				if self.state.writeServerBuf and self.state.writeServerBuf[0].tick():
					response: bytes = self.state.writeServerBuf.popleft().getBytes()
					self.connection.send(response)

				# Free the event loop to allow another decision
				await asyncio.sleep(0)

			# Break once the connection is closed
			except ConnectionClosedError as e:
				print(f'{RED}Connection lost...{NORMAL}', e)
				self.state.setConnectionStatus(False)
				break

def gpio_init():
	print("Setting up GPIO")
	GPIO.setmode(GPIO.BCM)
	GPIO.setup(14, GPIO.OUT)
	GPIO.setup(15, GPIO.OUT)
	GPIO.setup(18, GPIO.OUT)

last_selected_pos = (1,1)
# Main function
async def main():
	#gpio_init()
    
	# Start the debug server in the background
	global debug_server
	debug_server = DebugServer()
	asyncio.create_task(debug_server.run())
	DebugServer.instance = debug_server

	# Get the URL to connect to
	connectURL = getConnectURL()
	client = PacbotClient(connectURL)
	await client.run()
	
	# Once the connection is closed, end the event loop
	loop = asyncio.get_event_loop()
	loop.stop()

parser = argparse.ArgumentParser(description='Pacbot client that is the brains of the operation')
parser.add_argument('--debug', action='store_true', help='Enable debug mode, where the pathfinding is displayed')
parser.add_argument('--games', type=int, default=-1, help='Number of games to run, -1 for infinite')
parser.add_argument('--delay', type=int, default=0, help='Delay between games in milliseconds')
parser.add_argument('--output', type=str, default='', help='Output file for scores')

args = parser.parse_args()

if __name__ == '__main__':

	# Run the event loop forever
	loop = asyncio.get_event_loop()
	loop.create_task(main())
	loop.run_forever()
	
	print("Cleaning up GPIO")
	#GPIO.cleanup()
