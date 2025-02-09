from gameState import GameState
from debugServer import DebugServer
from utils import get_walkable_tiles, get_distance

class cellAvoidanceMap:
    def __init__(self, g: GameState):
        """
        The cellAvoidanceMap is a map of the game board that assigns a score to each cell based on how desirable it is to move to that cell.
        The more negative the score, the more desirable the cell is.
        We use the cellAvoidanceMap to power the heuristic function in the A* search algorithm, as well as for target selection.
        
        @param:
            - g, GameState object: the current game state
        """ 
        self.avoidance_map = {} # tuple (row, col) -> int
        self.g = g # the current game state
        
        # All parameters listed as tunable should be experimented with to find the best values.
        self.pellet_boost = 50        # TUNABLE
        self.superPellet_boost = 200  # TUNABLE
        self.fruit_boost = 600        # TUNABLE
        self.ghost_boost = 1000       # TUNABLE
        self.ghost_threshold_dist = 8 # TUNABLE
        self.fruit_threshold_dist = 5 # TUNABLE
        
        self.updateMap(self.g) # calculate the avoidance map based on the current game state
        
        
    def calculate_pellet_boost(self, tile):
        """
        Calculates the boost for normal pellets and super pellets based on the number of remaining pellets.
        
        @param:
            - tile, tuple: the tile to calculate the boost for, in the form (row, col)
            
        @return:
            - int: the boost for the tile
        """
        
        # TODO: account for how nearby ghosts are to the super pellet
        if self.g.superPelletAt(tile[0], tile[1]):
            if self.num_pellets <= 150:
                return self.superPellet_boost
            else:
                return 0
                    
        if not self.g.pelletAt(tile[0], tile[1]):
            return 0
        
        # TODO: account for clusters of pellets

        boost = self.pellet_boost

        # Aggressively boost pellet collection when few pellets remain
        # This could instead be a smooth exponential function.
        # The number of pellets at the beginning of a level is 240 for context.
        if self.num_pellets < 4:
            boost *= 50
        elif self.num_pellets < 6:
            boost *= 20
        elif self.num_pellets < 8:
            boost *= 10
        elif self.num_pellets < 10:
            boost *= 5
        elif self.num_pellets < 20:
            boost *= 4
        elif self.num_pellets < 50:
            boost *= 3
        elif self.num_pellets < 100:
            boost *= 2
        
        return boost
    
    def calculate_ghost_proximity(self, tile, ghost):
        """
        Calculate the proximity influence for a ghost at a given tile based on distance and ghost color.
        
        @param:
            - tile, tuple: the tile to calculate the ghost proximity for, in the form (row, col)
            - ghost, Ghost object: the ghost to calculate the proximity for
        
        @return:
            - int: the ghost proximity score for the tile
        """
        
        # TODO: use Ghost.guessPlan in gameState.py to account for differing ghost behaviors
        
        dist = get_distance(tile, (ghost.location.row, ghost.location.col))
        fright_modifier = -1 if ghost.isFrightened() else 1
        ghost_proximity = 0
        
        if dist < self.ghost_threshold_dist or (self.num_pellets < 10 and dist < self.ghost_threshold_dist / 2):
            if dist == 0 or dist is None:
                ghost_proximity = self.ghost_boost
            else:
                ghost_proximity = self.ghost_boost / (4 * dist) # TUNABLE
        
        return ghost_proximity * fright_modifier
    
    def calculate_fruit_boost(self, tile):
        """
        Calculate the boost for a fruit based on a tile's distance to the fruit and the remaining duration of the fruit.
        
        @param:
            - tile, tuple: the tile to calculate the fruit boost for, in the form (row, col)
        
        @return:
            - int: the fruit boost for the tile
        """
        
        fruit_boost = 0
        if self.g.fruitAt(self.g.fruitLoc.row, self.g.fruitLoc.col): # if there is a fruit on the board
            dist = get_distance(tile, (self.g.fruitLoc.row, self.g.fruitLoc.col))
            if dist == 0 or dist is None:
                fruit_boost = self.fruit_boost * ((self.g.fruitSteps + self.g.fruitDuration) / self.g.fruitDuration)
            elif dist < self.fruit_threshold_dist:
                fruit_boost = self.fruit_boost * ((self.g.fruitSteps + self.g.fruitDuration) / self.g.fruitDuration) / (dist * 2) # TUNABLE
                
        return fruit_boost
    
    def updateMap(self, g: GameState):
        """
        Calculate the avoidance map given the current state of the game.
        @param: 
            - g, GameState object
        """
        
        self.g = g
        self.avoidance_map = {}
        self.ghosts = self.g.ghosts
        self.num_pellets = self.g.numPellets()

        for tile in get_walkable_tiles(g): # we don't need to calculate an avoidance score for walls
            ghost_proximity = sum(self.calculate_ghost_proximity(tile, ghost) for ghost in self.ghosts)
            pellet_boost = self.calculate_pellet_boost(tile)
            fruit_boost = self.calculate_fruit_boost(tile)

            self.avoidance_map[tile] = ghost_proximity - pellet_boost - fruit_boost # this is the *avoidance* score, so the more positive the more we want to avoid it
    
    def show_map(self):
        """ Show the avoidance map on the debug server (in the terminal). """
        
        new_cell_colors = []
        for cell, score in self.avoidance_map.items():
            score = min(max(-255, score), 255)
            color = (score, 0, 0) if score > 0 else (0, -score, 0)
            new_cell_colors.append((cell, color))

        DebugServer.instance.set_cell_colors(new_cell_colors)
