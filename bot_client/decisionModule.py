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

from collections import deque

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

		self.prev_cells = [] # queue to keep track of the previous cells

		# If DistTable and DistTableDict don't exist, create them
		if not os.path.isfile('./static/distTable.json'):
			createDistTable(self.state)
		if not os.path.isfile('./static/dtDict.json'):
			createDistTableDict(self.state)

		# Load DistTable and DistTableDict
		self.distTable = loadDistTable()
		self.dtDict = loadDistTableDict()

	def update_target_loc(self, stuck=False): # optional param stuck, default is False
		'''
		Decide the direction to move in
		'''

		assert len(self.state.ghosts) != 0

		# Get the current position of Pacbot
		pacmanPos = (self.state.pacmanLoc.row, self.state.pacmanLoc.col)

		ghost_locations = list(map(lambda ghost: ghost.location, self.state.ghosts))

		# Find the point that is farthest from all the ghosts
		max_dist = 0
		max_dist_point = None

		for pos in self.walkable_cells:
			total_dist = 0

			for ghost_loc in ghost_locations:
				ghost_dist = get_distance(pos, (ghost_loc.row, ghost_loc.col))
				# make it a sum of the distance from the ghosts
				total_dist += ghost_dist

			if total_dist > max_dist:
				max_dist = total_dist
				max_dist_point = pos

		path = find_path(pacmanPos, max_dist_point, self.state)

		# print(path[0]) # print the first node on the path (which is where it plans to go)

		DebugServer.instance.set_path(path)
	
		if len(path) >= 1:
			self.targetPos = path[0] # just the closest / first node in path
		
		# otherwise, if len(path) = 0, does nothing
		# print("PATTHHHHHHHH")
		# print(len(path))

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
			print()
			print(f"Current: {self.state.pacmanLoc.row}, {self.state.pacmanLoc.col}")
			print(f"Target: {self.targetPos[0]}, {self.targetPos[1]}")
			print()

			# Calculate the delta between the current position and the target position
			deltaRow = self.targetPos[0] - self.state.pacmanLoc.row
			deltaCol = self.targetPos[1] - self.state.pacmanLoc.col
			absDelta = abs(deltaRow) + abs(deltaCol)

			# Perform the decision-making process
			if absDelta != 1:
				# This means it has reached the target (because 0 away)
				
				self.update_target_loc() # Update target loc
				self.prev_cells.append(1) # 1 means true, it is at the target loc
			else: 
				self.prev_cells.append(0)
				# then close enough to the target location
				# the issue is then it gets stuck here
				# e.g. for bottom left corner, it's 1 away from target and so it just stops
				print("TRIGGERED!")

				# Get the direction to move in
				direction = direction_from_delta(deltaRow, deltaCol)

				# Update our position on the server.
				# In the future, this needs to be replaced by a call to the low level movement code
				self.state.queueAction(1, direction)
				await asyncio.sleep(0.5)
			
			# check if stuck
			# meaning it's been in the same location for past 3 iterations

			if len(self.prev_cells) >= 3: # at least 3 iterations
				
				# in case we have more than 3 stored, need to restrict them
				while len(self.prev_cells) > 3:
					self.prev_cells.pop(0) # remove earlier iterations
	
				stuck = True
				num_checked = 0

				if num_checked < 3:
					for is_stuck in self.prev_cells:
						if is_stuck == 0:
							stuck = False
							break

						num_checked = 1
				
				if stuck:
					print("STUCK!!!!!! HELP ME PLEASE")
					self.prev_cells = [] # clear the list

					self.update_target_loc(stuck=True) # to force it to move, maybe pass in a param
					
			# Unlock the game state
			self.state.unlock()

			# Print that a decision has been made
			# print('decided')

			# Free up the event loop
			await asyncio.sleep(0)
