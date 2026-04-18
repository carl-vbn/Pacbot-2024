import asyncio
import os
import random
from time import time

from gameState import *
from debugServer import DebugServer
from utils import get_distance, get_walkable_tiles
from DistMatrix import createDistTable, createDistTableDict, loadDistTable, loadDistTableDict
from pathfinding import find_path
from AvoidanceMap import cellAvoidanceMap
import low_level
from low_level import send_direction, unstuck

# UNCOMMENT GPIO CODE IF RUNNING ON ACTUAL ROBOT (NOT SIMULATOR)
#import RPi.GPIO as GPIO

#GPIO.setmode(GPIO.BCM)
#GPIO.setup(14, GPIO.OUT)
#GPIO.setup(15, GPIO.OUT)
#GPIO.setup(18, GPIO.OUT)

def direction_from_delta(deltaRow, deltaCol):
	if deltaRow == 1:
		return Directions.DOWN
	elif deltaRow == -1:
		return Directions.UP
	elif deltaCol == 1:
		return Directions.RIGHT
	elif deltaCol == -1:
		return Directions.LEFT
	else:
		raise ValueError("Invalid delta")

def send_to_teensey(direction):
	bit_14 = 0
	bit_15 = 0
	bit_18 = 0
	if (direction == Directions.DOWN):
		bit_14=1
		bit_15=1
		bit_18=0
	elif (direction == Directions.UP):
		bit_14=1
		bit_15=0
		bit_18=0
	elif (direction == Directions.LEFT):
		bit_14=0
		bit_15=0
		bit_18=1
	elif (direction == Directions.RIGHT):
		bit_14=0
		bit_15=1
		bit_18=0
	elif (direction == Directions.RANDOM):
		bit_14 = 1
		bit_15 = 0
		bit_18 = 1

	#GPIO.output(14, bit_14)
	#GPIO.output(15, bit_15)
	#GPIO.output(18, bit_18)

class DecisionModule:
	'''
	Sample implementation of a decision module for high-level
	programming for Pacbot, using asyncio.
	'''

	def __init__(self, state: GameState, log: bool) -> None:
		self.state = state
		# Next tile Pacman should move to (never more than 1 cell from current pos)
		self.targetPos = (state.pacmanLoc.row, state.pacmanLoc.col)

		self.walkable_cells = get_walkable_tiles(state)
		self.avoidance_map = cellAvoidanceMap(state)

		if not os.path.isfile('./static/distTable.json'):
			createDistTable(self.state)
		if not os.path.isfile('./static/dtDict.json'):
			createDistTableDict(self.state)

		self.distTable = loadDistTable()
		self.dtDict = loadDistTableDict()
		self.log = log
		self.lastMovementTime = None
		self.STUCK_THRESHOLD = 3  # seconds
		self.prevLocation = (23, 13)

	def update_target_loc(self):
		assert len(self.state.ghosts) != 0

		pacmanPos = (self.state.pacmanLoc.row, self.state.pacmanLoc.col)
		if pacmanPos not in self.walkable_cells:
			return

		n = self.state.numPellets()
		self.avoidance_map.num_pellets = n
		self.avoidance_map.updateMap(self.state)

		if n > 100:
			radius = 10
		elif n > 50:
			radius = 15
		else:
			radius = 20

		avoidanceScores = {}
		for i in range(-radius, radius+1):
			for j in range(-radius, radius+1):
				cell = (pacmanPos[0] + i, pacmanPos[1] + j)
				if cell in self.avoidance_map.avoidance_map:
					avoidanceScores[cell] = (
						self.avoidance_map.avoidance_map[cell]
						+ get_distance(pacmanPos, cell)
					)

		if avoidanceScores:
			target = min(avoidanceScores, key=avoidanceScores.get)
		else:
			while True:
				rand_move = random.choice([
					(pacmanPos[0]+1, pacmanPos[1]),
					(pacmanPos[0]-1, pacmanPos[1]),
					(pacmanPos[0], pacmanPos[1]+1),
					(pacmanPos[0], pacmanPos[1]-1),
				])
				if rand_move in self.walkable_cells:
					target = rand_move
					break

		path = find_path(pacmanPos, target, self.state, self.avoidance_map, self.log)
		DebugServer.instance.set_path(path)

		if len(path) >= 1:
			self.targetPos = path[0]
		if self.log:
			print(f'Path: {path}')

	def get_direction(self) -> Directions:
		'''
		Compute the next direction using A* pathfinding.
		Recalculates the target when Pacman reaches it or gets stuck.
		'''
		pacmanPos = (self.state.pacmanLoc.row, self.state.pacmanLoc.col)

		if pacmanPos not in self.walkable_cells:
			return Directions.NONE

		# Stuck detection: if position hasn't changed for STUCK_THRESHOLD seconds,
		# pick a random valid neighbor as the new target
		if pacmanPos == self.prevLocation:
			if self.lastMovementTime is None:
				self.lastMovementTime = time()
			if (time() - self.lastMovementTime) > self.STUCK_THRESHOLD:
				if self.log:
					print('A*: stuck, choosing random neighbor')
				neighbors = [
					(pacmanPos[0] + 1, pacmanPos[1]),
					(pacmanPos[0] - 1, pacmanPos[1]),
					(pacmanPos[0], pacmanPos[1] + 1),
					(pacmanPos[0], pacmanPos[1] - 1),
				]
				valid = [n for n in neighbors if n in self.walkable_cells]
				if valid:
					self.targetPos = random.choice(valid)
		else:
			self.lastMovementTime = time()
			self.prevLocation = pacmanPos

		deltaRow = self.targetPos[0] - pacmanPos[0]
		deltaCol = self.targetPos[1] - pacmanPos[1]
		absDelta = abs(deltaRow) + abs(deltaCol)

		if absDelta != 1:
			self.update_target_loc()
			deltaRow = self.targetPos[0] - pacmanPos[0]
			deltaCol = self.targetPos[1] - pacmanPos[1]
			absDelta = abs(deltaRow) + abs(deltaCol)

		if absDelta == 1:
			return direction_from_delta(deltaRow, deltaCol)
		return Directions.NONE

	async def decisionLoop(self) -> None:
		'''
		Two modes depending on low_level.connected:
		  connected     — 0.2 s delay after each move
		  not connected — 3 pacbot moves per 2 ghost moves; ghosts move every
		                  12 ticks at 24 fps = 0.5 s, so sleep 1/3 s per move

		Calls send_direction only when the output direction changes.
		'''
		last_ticks = -1
		last_direction = Directions.NONE
		stuck_pos = None
		stuck_start = None
		unstuck_triggered = False

		while self.state.isConnected():
			if low_level.connected:
				# Physical robot: at most one move per server tick
				await asyncio.sleep(0)
				if self.state.currTicks == last_ticks:
					continue
				last_ticks = self.state.currTicks
			else:
				# Simulation: 3 pacbot moves per 2 ghost moves = 1 move per 1/3 s
				await asyncio.sleep(1 / 3)
				last_ticks = self.state.currTicks

			if self.state.gameMode == GameModes.PAUSED:
				continue

			if len(self.state.writeServerBuf):
				continue

			self.state.lock()

			direction = self.get_direction()
			pacman_pos = (self.state.pacmanLoc.row, self.state.pacmanLoc.col)

			if self.log:
				print(f'A* pos=({self.state.pacmanLoc.row},{self.state.pacmanLoc.col}) '
				      f'target={self.targetPos} dir={direction.name}')

			if direction != Directions.NONE:
				self.state.queueAction(1, direction)

			self.state.unlock()

			if direction != Directions.NONE:
				if pacman_pos != stuck_pos:
					unstuck_triggered = False
					stuck_pos = pacman_pos
					stuck_start = time()
				elif not unstuck_triggered and (time() - stuck_start) >= 2.0:
					unstuck_triggered = True
					await unstuck(self.state, stuck_pos)
			else:
				stuck_pos = None
				stuck_start = None
				unstuck_triggered = False

			if direction != last_direction:
				send_direction(direction)
				last_direction = direction

"""
================================================.
     .-.   .-.     .--.                         |
    | OO| | OO|   / _.-' .-.   .-.  .-.   .''.  |
    |   | |   |   \  '-. '-'   '-'  '-'   '..'  |
    '^^^' '^^^'    '--'                         |
===============.  .-.  .================.  .-.  |
               | |   | |                |  '-'  |
               | |   | |                |       |
               | ':-:' |                |  .-.  |
               |  '-'  |                |  '-'  |
==============='       '================'       |
"""
