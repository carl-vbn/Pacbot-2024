# Asyncio (for concurrency)
import asyncio

import os

# Game state
from gameState import *
from debugServer import DebugServer

# Import pathfinding utilities
from utils import get_distance, get_walkable_tiles
from DistMatrix import createDistTable, createDistTableDict, loadDistTable, loadDistTableDict
from pathfinding import find_path


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

		# If DistTable and DistTableDict don't exist, create them
		if not os.path.isfile('./static/distTable.json'):
			createDistTable(self.state)
		if not os.path.isfile('./static/dtDict.json'):
			createDistTableDict(self.state)

		# Load DistTable and DistTableDict
		self.distTable = loadDistTable()
		self.dtDict = loadDistTableDict()

	def update_target_loc(self):
		'''
		Decide the direction to move in
		'''

		assert len(self.state.ghosts) != 0

		# Get the current position of Pacbot
		pacmanPos = (self.state.pacmanLoc.row, self.state.pacmanLoc.col)

		ghost_locations = list(map(lambda ghost: ghost.location, self.state.ghosts))

		# Find the point that is farthest from any ghost
		max_dist = 0
		max_dist_point = None

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

		path = find_path(pacmanPos, max_dist_point, self.state)

		print(path[0]) # print the first node on the path (which is where it plans to go)

		DebugServer.instance.set_path(path)
	
		if len(path) > 1:
			self.targetPos = path[0] # just the closest / first node in path
		

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

			# PRINT CURRENT, TARGET VALUES
			print(f"Current: {self.state.pacmanLoc.row}, {self.state.pacmanLoc.col}")
			print(f"Target: {self.targetPos[0]}, {self.targetPos[1]}")

			# Calculate the delta between the current position and the target position
			deltaRow = self.targetPos[0] - self.state.pacmanLoc.row
			deltaCol = self.targetPos[1] - self.state.pacmanLoc.col
			absDelta = abs(deltaRow) + abs(deltaCol)

			# Perform the decision-making process
			if absDelta != 1:
				# Not adjacent to the target position
				# Recalculate the target + path
				self.update_target_loc()
			else:
				# Otherwise, if we are, then just update on server?

				# Get the direction to move in
				direction = direction_from_delta(deltaRow, deltaCol)

				# Update our position on the server.
				# In the future, this needs to be replaced by a call to the low level movement code
				self.state.queueAction(1, direction)
				await asyncio.sleep(0.5)
				

			# Unlock the game state
			self.state.unlock()

			# Print that a decision has been made
			# print('decided')

			# Free up the event loop
			await asyncio.sleep(0)
