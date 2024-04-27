# Asyncio (for concurrency)
import asyncio

import os
import random

# Game state
from gameState import *
from debugServer import DebugServer

# Import pathfinding utilities
from utils import get_distance, get_walkable_tiles
from DistMatrix import createDistTable, createDistTableDict, loadDistTable, loadDistTableDict
from pathfinding import find_path
from AvoidanceMap import cellAvoidanceMap
from GhostPrediction import GhostPrediction

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

class DecisionModule:
	'''
	Sample implementation of a decision module for high-level
	programming for Pacbot, using asyncio.
	'''

	def __init__(self, state: GameState) -> None:
		'''
		Construct a new decision module object
		'''

		# Game state object to store the game information
		self.state = state
		self.targetPos = (state.pacmanLoc.row, state.pacmanLoc.col) # The position we want Pacman to be at. Should never be more than 1 cell away from Pacman

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

		# Ghost prediction
		self.ghostPrediction = GhostPrediction(self.state)

	def update_target_loc(self):
		'''
		Decide the direction to move in
		'''

		assert len(self.state.ghosts) != 0

		# Get the current position of Pacbot
		pacmanPos = (self.state.pacmanLoc.row, self.state.pacmanLoc.col)
		if pacmanPos not in self.walkable_cells:
			return # If Pacman is not in a walkable cell, don't update the target location

		ghost_directions, ghost_locations = self.ghostPrediction.getGhostDirection(self.state)
		print(f"Ghost directions: {ghost_directions}")
		for idx, (ghost_col, ghost_dir) in enumerate(ghost_directions.items()):
			if ghost_dir != -1:
				ghost_dir_delta = self.ghostPrediction.delta_dict[ghost_dir]
				adj_probs = self.ghostPrediction.getGhostAdjacentProbs(self.state, ghost_col, ghost_locations[idx], ghost_dir_delta)
				for adj_cell, prob in adj_probs.items():
					print(f'{ghost_col} at {ghost_locations[idx]}: {adj_cell} with prob {prob}')

		# Within X block radius, set target to cell with lowest score in avoidance map
		self.avoidance_map.updateMap(self.state)
		num_pellets = self.avoidance_map.num_pellets

		# Pacbot evaluates more options as it gets closer to the end of the level
		if num_pellets > 200:
			radius = 5
		elif num_pellets > 100:
			radius = 10
		elif num_pellets > 50:
			radius = 15
		else:
			radius = 20

		radius = 5
		avoidanceScores = {}
		for i in range(-radius, radius+1):
			for j in range(-radius, radius+1):
				# If the cell is in the avoidance map, add it to the list of cells to consider
				if (pacmanPos[0] + i, pacmanPos[1] + j) in self.avoidance_map.avoidance_map:
					avoidanceScores[(pacmanPos[0] + i, pacmanPos[1] + j)] = self.avoidance_map.avoidance_map[(pacmanPos[0] + i, pacmanPos[1] + j)]
		
		# If there are cells to consider, set the target to the cell with the lowest score
		if avoidanceScores:
			target = min(avoidanceScores, key=avoidanceScores.get)
		else: # Move one space randomly
			while True:
				rand_move = random.choice([(pacmanPos[0]+1, pacmanPos[1]), (pacmanPos[0]-1, pacmanPos[1]), (pacmanPos[0], pacmanPos[1]+1), (pacmanPos[0], pacmanPos[1]-1)])
				if rand_move in self.walkable_cells:
					target = rand_move

		path = find_path(pacmanPos, target, self.state, self.avoidance_map)
		DebugServer.instance.set_path(path)
	
		if len(path) >= 1:
			self.targetPos = path[0]
		
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

			print(f"pacmanLoc ({self.state.pacmanLoc.row}, {self.state.pacmanLoc.col})  ->  targetPos ({self.targetPos[0]}, {self.targetPos[1]})")

			# Calculate the delta between the current position and the target position
			deltaRow = self.targetPos[0] - self.state.pacmanLoc.row
			deltaCol = self.targetPos[1] - self.state.pacmanLoc.col
			absDelta = abs(deltaRow) + abs(deltaCol)

			# Perform the decision-making process
			if absDelta != 1:
				# If we're at the target location, or the target locatipn is somehow unreachable from the current position,
				# update the target location
				self.update_target_loc()
			else:
				# Otherwise, move towards the target location

				# Get the direction to move in
				direction = direction_from_delta(deltaRow, deltaCol)

				# Update our position on the server.
				# In the future, this needs to be replaced by a call to the low level movement code
				self.state.queueAction(1, direction)
				await asyncio.sleep(0.1)
				

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