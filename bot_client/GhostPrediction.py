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

        for ghost in self.ghosts:
            self.prev_cells[ghost.color] = (ghost.location.row, ghost.location.col)
        
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

        
    