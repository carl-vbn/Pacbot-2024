from typing import Tuple, Dict, List
import math
import numpy as np
from gameState import GameState, GameModes, GhostColors, Directions
from debugServer import DebugServer
from utils import get_walkable_tiles, manhattan_distance
from heapq import heappush, heappop
import logging

# Set up logging
logging.basicConfig(level=logging.DEBUG, format='%(asctime)s - %(levelname)s - %(message)s')

class cellAvoidanceMap:
    """
    A potential-field map for Pac-Man encouraging movement:
      - Stronger attraction to pellets/fruit.
      - Weaker ghost repulsion unless close.
      - Momentum bias to prevent stalling.
    """

    PELLET_BOOST = 20          # Stronger pull to pellets
    SUPER_PELLET_BOOST = 200   # Stronger pull to super pellets
    FRUIT_BOOST = 1000         # Very strong fruit attraction
    GHOST_BOOST = 1500         # Reduced ghost repulsion
    GHOST_DECAY_ALPHA = 0.8    # Faster decay for ghost influence
    MOMENTUM_BIAS = 10         # Bias to keep moving in current direction

    GHOST_THRESHOLD_DIST = 8
    SUPERPELLET_GHOST_THRESHOLD = 7
    GHOST_IGNORE_DIST = 10

    def __init__(self, g: GameState) -> None:
        self.g = g
        self.avoidance_map: Dict[Tuple[int, int], float] = {}
        self.walkable_tiles = list(get_walkable_tiles(g))
        self.updateMap(self.g)

    def get_pellet_boost(self, tile: Tuple[int, int]) -> float:
        row, col = tile
        pellet_scaling = max(1.0, 50 / max(1, self.g.numPellets())) # we don't increase the pellet scaling until there are fewer than 50 pellets left, then it's inversely proportional to the number of pellets
        if self.g.superPelletAt(row, col):
            ghost_distances = [
                manhattan_distance(tile, (ghost.location.row, ghost.location.col))
                for ghost in self.g.ghosts if not ghost.isFrightened()
            ]
            ghost_distances = [d for d in ghost_distances if d is not None]
            if not ghost_distances or min(ghost_distances) > self.SUPERPELLET_GHOST_THRESHOLD:
                return -20  # Mild repulsion if no ghost nearby
            return self.SUPER_PELLET_BOOST * pellet_scaling
        if self.g.pelletAt(row, col):
            boost = self.PELLET_BOOST * pellet_scaling
            cluster_factor = self.compute_pellet_cluster(tile)
            return boost * cluster_factor
        return 0

    def compute_pellet_cluster(self, tile: Tuple[int, int], radius: int = 2) -> float:
        row, col = tile
        cluster_count = 0
        for r in range(row - radius, row + radius + 1):
            for c in range(col - radius, col + radius + 1):
                if self.g.pelletAt(r, c) or self.g.superPelletAt(r, c):
                    cluster_count += 1
        max_possible = (2 * radius + 1) ** 2 # TODO: maybe max_possible should instead be based on the number of walkable tiles in that radius
        return 1 + (cluster_count / max_possible)

    def get_ghost_proximity(self, tile: Tuple[int, int], ghost) -> float:
        dist = manhattan_distance(tile, (ghost.location.row, ghost.location.col))
        if dist is None or dist > self.GHOST_IGNORE_DIST:
            return 0
        decay = math.exp(-self.GHOST_DECAY_ALPHA * dist)
        fright_modifier = -1 if ghost.isFrightened() else 1
        lives_factor = max(1.0, 3 - self.g.currLives)
        # TODO: would love for there to be comments explaining the diff behaviors of the ghosts
        boost_factor = {
            GhostColors.RED: 1.2,
            GhostColors.PINK: 1.1 if dist < 5 else 0.8,
            GhostColors.CYAN: 0.9,
            GhostColors.ORANGE: 0.7
        }.get(ghost.color, 1.0)
        mode_factor = 1.0 if self.g.gameMode == GameModes.CHASE else 0.6
        return self.GHOST_BOOST * decay * fright_modifier * boost_factor * mode_factor * lives_factor

    def get_fruit_boost(self, tile: Tuple[int, int]) -> float:
        if self.g.fruitLoc and self.g.fruitAt(self.g.fruitLoc.row, self.g.fruitLoc.col):
            dist = manhattan_distance(tile, (self.g.fruitLoc.row, self.g.fruitLoc.col))
            if dist is None:
                return 0
            if dist == 0:
                return self.FRUIT_BOOST
            return self.FRUIT_BOOST / (dist + 1)
        return 0

    def get_momentum_bias(self, tile: Tuple[int, int]) -> float:
        """Bias toward Pac-Man's current direction to prevent stalling."""
        row, col = tile
        pacman_dir = self.g.pacmanLoc.getDirection()
        if pacman_dir != Directions.NONE:
            d_row, d_col = D_ROW[pacman_dir], D_COL[pacman_dir]
            next_pos = (self.g.pacmanLoc.row + d_row, self.g.pacmanLoc.col + d_col)
            # TODO: maybe we also want to add momentum bias to the tile after the next
            # TODO: or maybe we want to add negative momentum bias to the tile behind
            if (row, col) == next_pos and not self.g.wallAt(next_pos[0], next_pos[1]):
                return self.MOMENTUM_BIAS
        return 0

    def find_goal(self) -> Tuple[int, int]:
        ghost_distances = [
            manhattan_distance((self.g.pacmanLoc.row, self.g.pacmanLoc.col), 
                               (ghost.location.row, ghost.location.col))
            for ghost in self.g.ghosts if not ghost.isFrightened()
        ]
        ghost_distances = [d for d in ghost_distances if d is not None]
        # if ghosts are close, bait them and go for super pellets
        if ghost_distances and min(ghost_distances) < self.GHOST_THRESHOLD_DIST:
            super_pellets = [(r, c) for r, c in self.walkable_tiles if self.g.superPelletAt(r, c)] # TODO: we shouldn't need to check all walkable tiles to find the super pellets
            if super_pellets:
                return min(super_pellets, key=lambda t: manhattan_distance((self.g.pacmanLoc.row, self.g.pacmanLoc.col), t))
        pellets = [(r, c) for r, c in self.walkable_tiles if self.g.pelletAt(r, c)]
        # go for the biggest pellet cluster
        return max(pellets, key=lambda t: self.compute_pellet_cluster(t)) if pellets else (self.g.pacmanLoc.row, self.g.pacmanLoc.col)

    def a_star_boost(self, tile: Tuple[int, int], goal: Tuple[int, int]) -> float:
        def heuristic(a, b):
            return manhattan_distance(a, b)
        open_set = [(0, (self.g.pacmanLoc.row, self.g.pacmanLoc.col))]
        came_from = {}
        g_score = {tile: float('inf')}
        g_score[(self.g.pacmanLoc.row, self.g.pacmanLoc.col)] = 0
        f_score = {tile: float('inf')}
        f_score[(self.g.pacmanLoc.row, self.g.pacmanLoc.col)] = heuristic((self.g.pacmanLoc.row, self.g.pacmanLoc.col), goal)
        while open_set:
            current = heappop(open_set)[1]
            if current == goal:
                break
            for dr, dc in [(0, 1), (1, 0), (0, -1), (-1, 0)]:
                neighbor = (current[0] + dr, current[1] + dc)
                if neighbor not in self.walkable_tiles:
                    continue
                tentative_g = g_score[current] + 1
                if tentative_g < g_score.get(neighbor, float('inf')):
                    came_from[neighbor] = current
                    g_score[neighbor] = tentative_g
                    f_score[neighbor] = tentative_g + heuristic(neighbor, goal)
                    heappush(open_set, (f_score[neighbor], neighbor))
        dist_to_goal = g_score.get(tile, float('inf'))
        return -100 / (dist_to_goal + 1) if dist_to_goal != float('inf') else 0  # Reduced A* influence

    def updateMap(self, g: GameState) -> None:
        self.g = g
        self.avoidance_map.clear()
        self.walkable_tiles = list(get_walkable_tiles(g))
        self.num_pellets = self.g.numPellets()
        rows, cols = 31, 28  # Hardcoded from GameState
        field = np.zeros((rows, cols), dtype=float)
        goal = self.find_goal()
        ghost_distances = [
            manhattan_distance((self.g.pacmanLoc.row, self.g.pacmanLoc.col), 
                               (ghost.location.row, ghost.location.col))
            for ghost in self.g.ghosts if not ghost.isFrightened()
        ]
        ghost_distances = [d for d in ghost_distances if d is not None]

        for tile in self.walkable_tiles:
            r, c = tile
            ghost_influence = sum(self.get_ghost_proximity(tile, ghost) for ghost in self.g.ghosts)
            pellet_influence = self.get_pellet_boost(tile)
            fruit_influence = self.get_fruit_boost(tile)
            momentum_influence = self.get_momentum_bias(tile)
            path_influence = self.a_star_boost(tile, goal) if ghost_distances and min(ghost_distances) < self.GHOST_THRESHOLD_DIST else 0
            field[r, c] = ghost_influence - pellet_influence - fruit_influence - momentum_influence + path_influence
            logging.debug(f"Tile {tile}: ghost={ghost_influence:.2f}, pellet={pellet_influence:.2f}, fruit={fruit_influence:.2f}, momentum={momentum_influence:.2f}, path={path_influence:.2f}, total={field[r, c]:.2f}")

        # No smoothing to keep gradients sharp
        for tile in self.walkable_tiles:
            r, c = tile
            self.avoidance_map[tile] = field[r, c]

    def show_map(self) -> None:
        new_cell_colors: List[Tuple[Tuple[int, int], Tuple[int, int, int]]] = []
        for cell, score in self.avoidance_map.items():
            clamped_score = int(min(max(-255, score), 255))
            color = (clamped_score, 0, 0) if clamped_score > 0 else (0, -clamped_score, 0)
            new_cell_colors.append((cell, color))
            logging.debug(f"Cell {cell}: Score={score:.2f}, Color={color}")
        try:
            DebugServer.instance.set_cell_colors(new_cell_colors)
        except Exception as e:
            logging.error(f"DebugServer error: {e}")
