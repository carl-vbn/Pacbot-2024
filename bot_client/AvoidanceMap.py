import json
from json import JSONEncoder
import numpy as np

from gameState import GameState
from debugServer import DebugServer
from utils import get_walkable_tiles, get_distance
from DistMatrix import loadDistTable, loadDistTableDict


class cellAvoidanceMap:
    def __init__(self, g: GameState):
        """
        Creates instance of cellAvoidanceMap for a given GameState.
        Tunable parameters: ghost_proximity, pellet_boost
        """
        self.avoidance_map = {}
        self.g = g
        self.pellet_boost = 50 # TUNABLE
        self.superPellet_boost = 200 # TUNABLE
        self.fruit_boost = 600 # TUNABLE
        self.ghost_boost = 1000 # TUNABLE

        self.distTable = loadDistTable()
        self.dtDict = loadDistTableDict()
        
        self.num_pellets = g.numPellets()
        
        self.updateMap(self.g)
        
    
    def updateMap(self, g: GameState):
        """
        Reset map and ghost values
        @param: 
            - g, GameState object
        """
        self.g = g
        self.avoidance_map = {}

        # print(self.dtDict)
        # print(type(self.dtDict))
        
        self.ghosts = self.g.ghosts

        for tile in get_walkable_tiles(g):
            ghost_proximity = 0
            for ghost in self.ghosts:
                # TODO: Account for ghost color (i.e. avoid red ghost more than pink?)
                # try:
                #     tile_idx = self.dtDict[tile]
                #     ghost_idx = self.dtDict[ghost_pos]
                #     dist = self.distTable[tile_idx][ghost_idx]
                # except IndexError:
                #     dist = get_distance(tile, (ghost_pos[0], ghost_pos[1]))
                # except KeyError:
                #     dist = get_distance(tile, (ghost_pos[0], ghost_pos[1]))

                dist = get_distance(tile, (ghost.location.row, ghost.location.col))

                fright_modifier = 1
                if ghost.isFrightened():
                    fright_modifier = -1
                GHOST_THRESHOLD_DIST = 8 # tunable 
                if dist < GHOST_THRESHOLD_DIST:
                    #dist = get_astar_dist(tile, ghost_pos, self.g)
                    if dist == 0 or dist is None:
                        ghost_proximity += self.ghost_boost * fright_modifier
                    else:
                        ghost_proximity += self.ghost_boost / (4 * dist) * fright_modifier  # Tunable

            # TODO: Maybe account for distance to nearby pellets?
            pellet_boost = 0
            if self.g.pelletAt(tile[0], tile[1]):
                pellet_boost = self.pellet_boost
                
                # Tunable: pellet boost multiplier based on number of pellets left
                # pacbot needs to be more aggressive as it gets closer to the end of the level
                # this doesn't have to be a series of if statements, could be a function lol
                if self.num_pellets < 4:
                    pellet_boost *= 50
                elif self.num_pellets < 6:
                    pellet_boost *= 20
                elif self.num_pellets < 8:
                    pellet_boost *= 10
                elif self.num_pellets < 10:
                    pellet_boost *= 5
                elif self.num_pellets < 20:
                    pellet_boost *= 4
                elif self.num_pellets < 50:
                    pellet_boost *= 3
                elif self.num_pellets < 100:
                    pellet_boost *= 2
                
            if self.g.superPelletAt(tile[0], tile[1]):
                pellet_boost = self.superPellet_boost
            
            fruit_boost = 0
            if self.g.fruitAt(self.g.fruitLoc.row, self.g.fruitLoc.col): # if there is a fruit on the board
                dist = get_distance(tile, (self.g.fruitLoc.row, self.g.fruitLoc.col))
                FRUIT_THRESHOLD_DIST = 5
                if dist == 0 or dist is None:
                    fruit_boost = self.fruit_boost * ((self.g.fruitSteps + self.g.fruitDuration) / self.g.fruitDuration)
                elif dist < FRUIT_THRESHOLD_DIST:
                    fruit_boost = self.fruit_boost * ((self.g.fruitSteps + self.g.fruitDuration) / self.g.fruitDuration) / (dist * 2)
                

            self.avoidance_map[tile] = ghost_proximity - pellet_boost - fruit_boost # negative is good, positive is bad
    
    def show_map(self):
        """
        Show the avoidance map on the debug server.
        """
        new_cell_colors = []
        for cell, score in self.avoidance_map.items():
            score = min(max(-255, score), 255)
            color = (score, 0, 0) if score > 0 else (0, -score, 0)
            new_cell_colors.append((cell, color))

        DebugServer.instance.set_cell_colors(new_cell_colors)
