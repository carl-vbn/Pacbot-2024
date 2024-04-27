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

        self.ghost_dict = {0: 'Red', 1: 'Pink', 2: 'Cyan', 3: 'Orange'}

        # Absolute directions and the corresponding 'deltas'
        # Current_tile (tuple) + delta = Prev tile assuming ghost direction = delta_dict[delta]
        self.deltas = [(1, 0), (-1, 0), (0, 1), (0, -1)] # Up, Down, Left, Right
        self.delta_dict = {0: 'up', 1: 'down', 2: 'left', 3: 'right'}

        # Adjacency table, indexable relative new directions for each previous direction
        # Key: prev_dir, Value: ['forward', 'backward', 'left', 'right']
        self.adj_table = {
            'up': ['up', 'down', 'left', 'right'],
            'down': ['down', 'up', 'right', 'left'],
            'left': ['left', 'right', 'down', 'up'],
            'right': ['right', 'left', 'up', 'down']
        }
        # Current_tile + direction = New tile in direction
        self.dir_to_delta = {'up': (-1, 0), 'down': (1, 0), 'left': (0, -1), 'right': (0, 1)}

        for ghost in self.ghosts:
            ghost_color = self.ghost_dict[ghost.color]
            self.prev_cells[ghost_color] = (ghost.location.row, ghost.location.col)
        
    def getGhostDirection(self, g: GameState):
        """
        Predicts the direction of the ghost
        """
        new_ghosts = g.ghosts
        ghost_locations = [(ghost.location.row, ghost.location.col) for ghost in new_ghosts]
        
        # print(f'predicting... old pos {self.prev_cells}\nnew pos {new_ghosts}')
        ghost_directions = {}
        for ghost in new_ghosts:
            ghost_color = self.ghost_dict[ghost.color]
            if (ghost.location.row, ghost.location.col) != self.prev_cells[ghost_color]:
                for idx, delta in enumerate(self.deltas):
                    if (ghost.location.row + delta[0], ghost.location.col + delta[1]) == self.prev_cells[ghost_color]:
                        ghost_directions[ghost_color] = idx
                        print(f'Ghost {ghost_color} moved {self.delta_dict[idx]}')
                        break
            else:
                ghost_directions[ghost_color] = -1 # Ghost did not move lol
            
            # Update the previous cell
            self.prev_cells[ghost_color] = (ghost.location.row, ghost.location.col)
        
        return ghost_directions, ghost_locations
    
    def getGhostAdjacentProbs(self, g: GameState, ghost_col, ghost_pos, prev_dir):
        """
        Returns the probability of a ghost being in each adjacent tile
        Code is not by any means optimized lmao
        """
        walkable_tiles = get_walkable_tiles(g)
        adj_probs = {}

        adj_cells = [(ghost_pos[0] + delta[0], ghost_pos[1] + delta[1]) for delta in self.deltas] # Get all 4 adj cells
        adj_cells = [cell for cell in adj_cells if cell in walkable_tiles] # Remove non-walkable cells (walls)

        n_adj = len(adj_cells)
        forward_dir_delta = self.dir_to_delta[self.adj_table[prev_dir][0]]
        forward_dir_tile = (ghost_pos[0] + forward_dir_delta[0], ghost_pos[1] + forward_dir_delta[1])

        # same logic for here
        left_dir_tile = (ghost_pos[0] + self.dir_to_delta[self.adj_table[prev_dir][2]][0], ghost_pos[1] + self.dir_to_delta[self.adj_table[prev_dir][2]][1])
        right_dir_tile = (ghost_pos[0] + self.dir_to_delta[self.adj_table[prev_dir][3]][0], ghost_pos[1] + self.dir_to_delta[self.adj_table[prev_dir][3]][1])
        back_dir_tile = (ghost_pos[0] + self.dir_to_delta[self.adj_table[prev_dir][1]][0], ghost_pos[1] + self.dir_to_delta[self.adj_table[prev_dir][1]][1])
        
        if n_adj == 2:
            if forward_dir_tile in adj_cells:
                # tunnel probabilities
                print(f'{ghost_col}: tunnel')
                adj_probs[forward_dir_tile] = 0.9
                adj_probs[back_dir_tile] = 0.1
            else:
                # corner probabilities
                print(f'{ghost_col}: corner')
                if left_dir_tile in adj_cells:
                    adj_probs[left_dir_tile] = 0.9
                elif right_dir_tile in adj_cells:
                    adj_probs[right_dir_tile] = 0.9
                adj_probs[back_dir_tile] = 0.1
                
        elif n_adj == 3:
            if forward_dir_tile in adj_cells:
                # ㅓ, ㅏ probabilities 
                print(f'{ghost_col}: ㅓ, ㅏ')
                adj_probs[forward_dir_tile] = 0.5
                if left_dir_tile in adj_cells:
                    adj_probs[left_dir_tile] = 0.3
                elif right_dir_tile in adj_cells:
                    adj_probs[right_dir_tile] = 0.3
                adj_probs[back_dir_tile] = 0.2

            else:
                # T probabilities
                print(f'{ghost_col}: T')
                adj_probs[left_dir_tile] = 0.4
                adj_probs[right_dir_tile] = 0.4
                adj_probs[back_dir_tile] = 0.2

        elif n_adj == 4:
            # + probabilities 
            print(f'{ghost_col}: +')
            adj_probs[forward_dir_tile] = 0.3
            adj_probs[left_dir_tile] = 0.3
            adj_probs[right_dir_tile] = 0.3
            adj_probs[back_dir_tile] = 0.1

        else:
            # Wtf
            print(f'{ghost_col}: wtf')
            adj_probs[ghost_pos] = -1

        return adj_probs
        
            






        
    