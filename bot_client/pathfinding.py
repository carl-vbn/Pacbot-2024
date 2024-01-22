import math
from gameState import GameState
from debugServer import DebugServer

def get_distance(posA, posB):
    rowA,colA = posA if type(posA) == tuple else (posA.row, posA.col)
    rowB,colB = posB if type(posB) == tuple else (posB.row, posB.col)

    drow = rowA - rowB
    dcol = colA - colB
    dist = math.sqrt(dcol * dcol + drow * drow)
    return dist

def estimate_heuristic(node_pos, target_pos, cell_avoidance_map):
    return get_distance(node_pos, target_pos) + (cell_avoidance_map[node_pos] if cell_avoidance_map is not None else 0)

def get_neighbors(g: GameState, location=None):
    if location is None:
        location = g.pacmanLoc
    
    row,col = location if type(location) == tuple else (location.row, location.col)
    neighbors = []
    if not g.wallAt(row + 1,col):
        neighbors.append((row + 1,col))
    if not g.wallAt(row - 1,col):
        neighbors.append((row - 1,col))
    if not g.wallAt(row, col + 1):
        neighbors.append((row, col + 1))
    if not g.wallAt(row, col - 1):
        neighbors.append((row, col - 1))
    return neighbors

def get_walkable_tiles(g: GameState):
	walkable_cells = set()
	for row in range(31):
		for col in range(28):
			if not g.wallAt(row, col):
				walkable_cells.add((row, col))
	return walkable_cells

def nearby_super_pellet_bonus(g: GameState, location):
    row,col = location if type(location) == tuple else (location.row, location.col)
    super_pellet_locations = [(3, 1), (3, 26), (23, 1), (23, 26)]
    bonus = 0
    for super_pellet_location in super_pellet_locations:
        if g.pelletAt(super_pellet_location[0], super_pellet_location[1]): # super pellet may have been eaten
            # the closer we are to the super pellet, the higher the bonus
            dist = get_distance(location, super_pellet_location)
            if dist < 10:
                bonus += 150 / dist
    return bonus

def build_cell_avoidance_map(g: GameState):
    cell_avoidance_map = {}

    for tile in get_walkable_tiles(g):
        total_ghost_proximity = 0
        for ghost in g.ghosts:
            dist = get_distance(tile, (ghost.location.row, ghost.location.col))
            if dist == 0:
                ghost_proximity = 1000
            else:
                ghost_proximity = 750 / dist
            if ghost.isFrightened():
                ghost_proximity = -ghost_proximity*2
            if ghost.spawning:
                ghost_proximity /= 10
            total_ghost_proximity += ghost_proximity
        pellet_boost = 0
        if g.pelletAt(tile[0], tile[1]):
            pellet_boost = 50
        if g.superPelletAt(tile[0], tile[1]):
            pellet_boost = 250
        else:
            pellet_boost += nearby_super_pellet_bonus(g, tile)

        cell_avoidance_map[tile] = total_ghost_proximity - pellet_boost

    return cell_avoidance_map

def show_cell_avoidance_map(cell_avoidance_map):
    new_cell_colors = []
    for cell, score in cell_avoidance_map.items():
        score = min(max(-255, score), 255)
        color = (score, 0, 0) if score > 0 else (0, -score, 0)
        new_cell_colors.append((cell, color))

    DebugServer.instance.set_cell_colors(new_cell_colors)

def find_path(start, target, g: GameState):
    cell_avoidance_map = build_cell_avoidance_map(g)
    show_cell_avoidance_map(cell_avoidance_map)

    open_nodes = set()
    open_nodes.add(start)

    parents = {}

    g_map = {}
    g_map[start] = 0

    f_map = {}
    f_map[start] = estimate_heuristic(start, target, cell_avoidance_map)

    while len(open_nodes) > 0:
        # Find the node with the lowest f score
        current = None
        current_f = None
        for node, score in f_map.items():
            if node in open_nodes and (current is None or score < current_f):
                current = node
                current_f = score

        if current == target:
            path = []
            while current in parents:
                path.append(current)
                current = parents[current]
            path.reverse()
            return tuple(path)
        
        open_nodes.remove(current)

        for neighbor in get_neighbors(g, current):
            tentative_gScore = g_map[current] + get_distance(current, neighbor)
            if neighbor not in g_map or tentative_gScore < g_map[neighbor]:
                parents[neighbor] = current
                g_map[neighbor] = tentative_gScore
                f_map[neighbor] = tentative_gScore + estimate_heuristic(neighbor, target, cell_avoidance_map)
                open_nodes.add(neighbor)

    return None

if __name__ == '__main__':
    g = GameState()

    start = (1,1)
    target = (6,6)

    path = find_path(start, target, g)
    print(path)
