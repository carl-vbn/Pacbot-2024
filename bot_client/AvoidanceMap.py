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
        self.pellet_boost = 50
        self.superPellet_boost = 200

        self.distTable = loadDistTable()
        self.dtDict = loadDistTableDict()

        self.num_pellets = g.numPellets()
        self.threshold_dist = 8
        
        self.updateMap(self.g)
        
    
    def updateMap(self, g: GameState):
        """
        Reset map and ghost values
        @param: 
            - g, GameState object
        """
        self.g = g
        self.avoidance_map = {}
        self.num_pellets = g.numPellets()

        # print(self.dtDict)
        # print(type(self.dtDict))
        
        self.ghosts = self.g.ghosts

        for tile in get_walkable_tiles(g):
            ghost_proximity = 0
            for ghost in self.ghosts:
                # TODO: Account for ghost color (i.e. avoid red ghost more than pink?)

                dist = get_distance(tile, (ghost.location.row, ghost.location.col))

                fright_modifier = 1
                if ghost.isFrightened():
                    fright_modifier = -1
                
                if dist < self.threshold_dist:
                    if dist == 0 or dist is None:
                        ghost_proximity += 1000*fright_modifier  # If ghost AT cell
                    else:
                        ghost_proximity += (1/dist) * 250 * fright_modifier  # Tunable

            # TODO: Maybe account for distance to nearby pellets?
            pellet_boost = 0
            # Normal pellet boost (increases as num_pellets decrease)
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

            # Super pellet boost 
            if self.g.superPelletAt(tile[0], tile[1]):
                pellet_boost = self.superPellet_boost
            
            self.avoidance_map[tile] = ghost_proximity - pellet_boost

    
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
