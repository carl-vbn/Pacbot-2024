import json
from json import JSONEncoder
import numpy as np

from gameState import GameState
from debugServer import DebugServer
from utils import get_walkable_tiles, get_distance
from DistMatrix import loadDistTable, loadDistTableDict


class GhostPrediction:
    def __init__(self, g: GameState):
        self.ghosts = g.ghosts
        self.g = g
        self.prev_cells = {}
        self.probMap = self.initProb(g)

        for ghost in self.ghosts:
            self.prev_cells[ghost.color] = (ghost.location.row, ghost.location.col)

        #self.getProb(g, (16,21))
        print(self.probMap)
        
    def getGhostDirection(self, g: GameState):
        """
        Predicts the direction of the ghost
        """
        new_ghosts = g.ghosts
        deltas = [(-1, 0), (1, 0), (0, -1), (0, 1)] # Up, Down, Left, Right
        print(f'predicting... old pos {self.prev_cells}\nnew pos {new_ghosts}')
        ghost_directions = {}
        for ghost in new_ghosts:
            if (ghost.location.row, ghost.location.col) != self.prev_cells[ghost.color]:
                for idx, delta in enumerate(deltas):
                    if (ghost.location.row + delta[0], ghost.location.col + delta[1]) == self.prev_cells[ghost.color]:
                        ghost_directions[ghost.color] = idx
                        print(f'Ghost {ghost.color} moved {idx}')
                        break
            else:
                ghost_directions[ghost] = -1 # Ghost did not move
            
            # Update the previous cell
            self.prev_cells[ghost.color] = (ghost.location.row, ghost.location.col)
        
        return ghost_directions
    
    
    def initProb(self, g: GameState):
        """
        Creates a generic map of probabilities which does not factor in ghost direction.
        That way, we don't have to go through every square everytime we check a ghost.
        """
        walkable_tiles = get_walkable_tiles(g)
        
        tiles = {}
        
        for tile in walkable_tiles:
            probabilities = {}

            up = (tile[0]-1, tile[1])
            down = (tile[0]+1, tile[1])
            left = (tile[0], tile[1]-1)
            right = (tile[0], tile[1]+1)
            
            if(up in walkable_tiles):
                probabilities[up] = 0.0
            if(down in walkable_tiles):
                probabilities[down] = 0.0
            if(left in walkable_tiles):
                probabilities[left] = 0.0
            if(right in walkable_tiles):
                probabilities[right] = 0.0
            
            even_probability = 1/len(probabilities)
            
            for key in probabilities:
                probabilities[key] = even_probability

            tiles[tile] = probabilities
    
        return tiles
    
    def getProb(self, g: GameState, probMap, position, direction):
        """Queries the map and factors into account ghost direction. Did we ever figure out getting ghost direction?"""

        availableMoves = probMap[position] #availableMoves is a dictionary with max 4 entries
        
        LOWEST_PROBABILITY = 0.1
        REMAINING_PROBABILITY = 1 - LOWEST_PROBABILITY
        LOWEST_SQUARE = ()

        up = (position[0]-1, position[1])
        down = (position[0]+1, position[1])
        left = (position[0], position[1]-1)
        right = (position[0], position[1]+1)

        if(direction == 0 and (down in availableMoves)):
            LOWEST_SQUARE = down
            availableMoves[down] = LOWEST_PROBABILITY
        elif(direction == 1 and (right in availableMoves)):
            LOWEST_SQUARE = right
            availableMoves[right] = LOWEST_PROBABILITY
        elif(direction == 2 and (up in availableMoves)):
            LOWEST_SQUARE = up
            availableMoves[up] = LOWEST_PROBABILITY
        elif(direction == 3 and (left in availableMoves)):
            LOWEST_SQUARE = left
            availableMoves[left] = LOWEST_PROBABILITY

        #Equally divided among remaining directions
        REMAINING_PROBABILITY = (1-LOWEST_PROBABILITY)/(len(availableMoves)-1)
        
        for key in availableMoves:
            if(key != LOWEST_SQUARE):
                availableMoves[key] = REMAINING_PROBABILITY

        return availableMoves


    
    

        
    