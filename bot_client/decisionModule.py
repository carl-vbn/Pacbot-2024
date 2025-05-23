# Asyncio (for concurrency)
import asyncio

import os
import random
from time import time

# Game state
from gameState import *
from debugServer import DebugServer

# Import pathfinding utilities
from utils import get_distance, get_walkable_tiles
from DistMatrix import createDistTable, createDistTableDict, loadDistTable, loadDistTableDict
from pathfinding import find_path
from AvoidanceMap import cellAvoidanceMap

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
		'''
		Construct a new decision module object
		'''

		# Game state object to store the game information
		self.state = state
   		# The position we want Pacman to be at. Should never be more than 1 cell away from Pacman
		# This is the next tile Pacman should move to, NOT the end goal of the path.
		self.targetPos = (state.pacmanLoc.row, state.pacmanLoc.col)

		self.walkable_cells = get_walkable_tiles(state)
		self.avoidance_map = cellAvoidanceMap(state)

		# If DistTable and DistTableDict don't exist, create them
		if not os.path.isfile('./static/distTable.json'):
			createDistTable(self.state)
		if not os.path.isfile('./static/dtDict.json'):
			createDistTableDict(self.state)

		# Load DistTable and DistTableDict
		self.distTable = loadDistTable()
		self.dtDict = loadDistTableDict()
		self.log = log
		self.lastMovementTime = None
		self.STUCK_THRESHOLD = 3 # seconds
		self.prevLocation = (23, 13) # initial location of pacbot

	def update_target_loc(self):
		'''
		Decide the direction to move in
		'''

		assert len(self.state.ghosts) != 0

		# Get the current position of Pacbot
		pacmanPos = (self.state.pacmanLoc.row, self.state.pacmanLoc.col)
		if pacmanPos not in self.walkable_cells:
			return # If Pacman is not in a walkable cell, don't update the target location

		ghost_locations = list(map(lambda ghost: ghost.location, self.state.ghosts))

		# Find the point that is farthest from any ghost
		max_dist = 0
		max_dist_point = None

		""" Old way: furthest distance from closest ghost
		for pos in self.walkable_cells:
			dist_to_closest_ghost = None
			for ghost_loc in ghost_locations:
				dist = get_distance(pos, (ghost_loc.row, ghost_loc.col))
				# try:
				# 	pos_idx = self.dtDict[pos]
				# 	ghost_idx = self.dtDict[(ghost_loc.row, ghost_loc.col)]
				# 	print(f'pos: {pos_idx}, ghost_pos: {ghost_idx}')
				# 	print(type(pos_idx), type(ghost_idx))
				# 	dist = self.distTable[pos_idx][ghost_idx]
				# except IndexError:
				# 	dist = get_distance(pos, (ghost_loc.row, ghost_loc.col))
				# except KeyError:
				# 	dist = get_distance(pos, (ghost_loc.row, ghost_loc.col))

				if dist_to_closest_ghost is None or dist < dist_to_closest_ghost:
					dist_to_closest_ghost = dist

			if dist_to_closest_ghost > max_dist:
				max_dist = dist_to_closest_ghost
				max_dist_point = pos
		path = find_path(pacmanPos, max_dist_point, self.state, self.avoidance_map)
		"""

		n = self.state.numPellets()
		self.avoidance_map.num_pellets = n
		self.avoidance_map.updateMap(self.state)
  
		# pacbot evaluates more options as it gets closer to the end of the level
		# (tunable ofc)
		if n > 100:
			radius = 10
		elif n > 50:
			radius = 15
		else:
			radius = 20
   
   		# Testing new way: within X block radius, set target to cell with lowest score in avoidance map
		avoidanceScores = {}
		for i in range(-radius, radius+1):
			for j in range(-radius, radius+1):
				# If the cell is in the avoidance map, add it to the list of cells to consider
				if (pacmanPos[0] + i, pacmanPos[1] + j) in self.avoidance_map.avoidance_map:
					# we add the distance to the score to help with pacbot's indecisiveness
					avoidanceScores[(pacmanPos[0] + i, pacmanPos[1] + j)] = self.avoidance_map.avoidance_map[(pacmanPos[0] + i, pacmanPos[1] + j)] + get_distance(pacmanPos, (pacmanPos[0] + i, pacmanPos[1] + j))
		
		# If there are cells to consider, set the target to the cell with the lowest score
		if avoidanceScores:
			target = min(avoidanceScores, key=avoidanceScores.get)
		else: # Move one space randomly
			while True:
				rand_move = random.choice([(pacmanPos[0]+1, pacmanPos[1]), (pacmanPos[0]-1, pacmanPos[1]), (pacmanPos[0], pacmanPos[1]+1), (pacmanPos[0], pacmanPos[1]-1)])
				if rand_move in self.walkable_cells:
					target = rand_move

		path = find_path(pacmanPos, target, self.state, self.avoidance_map, self.log)
		DebugServer.instance.set_path(path)
	
		if len(path) >= 1:
			self.targetPos = path[0]
		if self.log:
			print(f"Path: {path}")
		

	async def decisionLoop(self) -> None:
		'''
		Decision loop for Pacbot
		'''

		# Receive values as long as we have access
		while self.state.isConnected():
			'''
			WARNING: 'await' statements should be routinely placed
			to free the event loop to receive messages, or the
			client may fall behind on updating the game state!
			'''

			# If the current messages haven't been sent out yet, skip this iteration
			if len(self.state.writeServerBuf):
				await asyncio.sleep(0)
				continue


			# Lock the game state
			self.state.lock()

			if self.log:
				print(f"Current: {self.state.pacmanLoc.row}, {self.state.pacmanLoc.col}")
				print(f"Target: {self.targetPos[0]}, {self.targetPos[1]}")

			# Calculate the delta between the current position and the target position
			deltaRow = self.targetPos[0] - self.state.pacmanLoc.row
			deltaCol = self.targetPos[1] - self.state.pacmanLoc.col
			absDelta = abs(deltaRow) + abs(deltaCol)
   
			direction = None

			# Perform the decision-making process
			if absDelta != 1:
				# If we're at the target location, or the target locatipn is somehow unreachable from the current position,
				# update the target location
				self.update_target_loc()
			else:
				if self.state.pacmanLoc.row == self.prevLocation[0] and self.state.pacmanLoc.col == self.prevLocation[1]:
					# only start tracking time once we are playing
					if self.lastMovementTime is None:
						self.lastMovementTime = time()
					if self.log:
						print(f"Stuck for {time() - self.lastMovementTime} seconds")
					if (time() - self.lastMovementTime) > self.STUCK_THRESHOLD:
						# If we are stuck for more than STUCK_THRESHOLD seconds, move randomly
						if self.log:
							print("GOING RANDOM.")
						direction = Directions.RANDOM
				
    			# Otherwise, move towards the target location
				if direction is None:
					# Get the direction to move in
					direction = direction_from_delta(deltaRow, deltaCol)

					# Update our position on the server.
					# !TODO: In the future, this needs to be replaced by a call to the low level movement code
					self.state.queueAction(1, direction)
				if self.state.gameMode != GameModes.PAUSED:
					send_to_teensey(direction)
				else:
					send_to_teensey(Directions.NONE)
				if self.prevLocation != (self.state.pacmanLoc.row, self.state.pacmanLoc.col) or self.state.gameMode == GameModes.PAUSED:
					self.lastMovementTime = time()
					self.prevLocation = (self.state.pacmanLoc.row, self.state.pacmanLoc.col)
				await asyncio.sleep(0.2)
				


				
				
			# Unlock the game state
			self.state.unlock()

			# Print that a decision has been made
			# print('decided')

			# Free up the event loop
			await asyncio.sleep(0)

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
