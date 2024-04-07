from gameState import GameState
import json

# # Load dtDict.json
# with open('dtDict.json', 'r') as f:
#     dt_dict = json.load(f)

# # Load distTable.json
# with open('distTable.json', 'r') as f:
#     dist_table = json.load(f)


def isIntersection(g: GameState, row: int, col: int) -> bool:
    above = g.wallAt(row, col+1)
    below = g.wallAt(row, col-1)
    left = g.wallAt(row-1, col)
    right = g.wallAt(row+1, col)
    # 4 way intersection
    if above and below and left and right:
        return True
    # 3 way intersection
    if above and below and left:
        return True
    if above and below and right:
        return True
    if above and left and right:
        return True
    if below and left and right:
        return True
    # 2 way intersection
    if above and right:
        return True
    if above and left:
        return True
    if below and right:
        return True
    if below and left:
        return True
    return False


def createGraph(g: GameState):
    # create a graph (adjacency list representation) of all the intersections, and distances and values between 
    # graph = {
    #   (row_1, col_1): {(row_n, col_n): [distance, value], 
    #                   (row_k, col_k): [distance, value], ...}, ...
    # } 
    intersections = set() # list of intersections
    graph = {} # adjacency list representation

    # find and create a list of all intersections
    for row in range(g.height):
        for col in range(g.width):
            if not g.wallAt(row, col):
                if isIntersection(g, row, col):
                    intersections.add((row, col))
    
    for intersect in intersections:
        directions = [(0, 1), (0, -1), (-1, 0), (1, 0)]
        for i in directions:
            dist = 0
            row = intersect[0]
            col = intersect[1]
            while not g.wallAt(row, col): # keep moving in the specified direction until you hit a wall or another intersection
                row += i[0] 
                col += i[1]
                dist += 1
                if (row, col) in intersections:
                    if intersect not in graph:
                        graph[intersect] = {(row, col): [dist, 0]}
                    else:
                        graph[intersect][(row, col)] =  [dist, 0]
                    break
    return graph

def updateGhostValue(graph, intersect1, intersect2, new_value):
    graph[intersect1][intersect2][1] = new_value
    graph[intersect2][intersect1][1] = new_value