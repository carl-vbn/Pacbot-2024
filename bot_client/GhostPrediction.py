import json
from json import JSONEncoder
import numpy as np

from gameState import GameState
from debugServer import DebugServer
from utils import get_walkable_tiles, get_distance
from DistMatrix import loadDistTable, loadDistTableDict


class ghostPrediction:
    def __init__(self, g: GameState):
        self.ghost_positions = ()
        self.g = g

        self.prev_cells = [] # queue to keep track of the previous cells

        '''
        self.pellet_boost = 50
        self.superPellet_boost = 200

        self.distTable = loadDistTable()
        self.dtDict = loadDistTableDict()
        
        self.updateMap(self.g)
        '''
        
    def printDirections(self, g: GameState):
        # first, we want to append the new ghost positions
        # ghost_positions = 

        self.prev_cells.append(1) # 1 means true, it is at the target loc

        self.ghosts = self.g.ghosts

        # self.ghosts.color = # keep track of the color

        # could just have a dictionary corresponding to each color
        # then it points to a tuple of the previous cell

        # self.ghost_positions = list(map(lambda ghost: (ghost.location.row, ghost.location.col), g.ghosts))

        # where is it getting ghost_position?
        
        for ghost in self.ghosts:
            print(ghost.plannedDirection)

        # GameState
        
        # check if stuck
        # meaning it's been in the same location for past 3 iterations
        
        '''
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
                
    '''

    