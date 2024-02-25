import math
from gameState import GameState
from debugServer import DebugServer
import pathfinding
import numpy as np

class RLLearn_SARAS():
    def __init__(self, addr, port):
        # self.subscriptions = [MsgType.LIGHT_STATE]
        # super().__init__(addr, port, message_buffers, MsgType, FREQUENCY, self.subscriptions)
        # self.state = None
        # self.grid = copy.deepcopy(grid)

        # self.policy = np.zeros((grid.shape, 4))
        # self.lr = 0.01
        # self.min_lr = 0.001
        # self.lr_decay = 0.99
        # self.gamma = 1
        # self.eps = 0.1
        # self.eps_decay = 0.999
        # self.min_eps = 0.001
        # self.Q = np.zeros() # the policy
        # self.gamma = 0
        # self.alpha = 0
        pass


    def update_game_state(self):
        pass

    def get_reward(self):
        done = self.is_done() #return true if eaten all coins
        dead = self.is_dead()
        pass

    def get_action_greedy(self, state):
        action = 0
        return action
    
    def update_Q(self):
        pass
    
    def is_done(self):
        pass
    
    def is_dead(self, state):
        pass

    #Function to learn the Q-value 
    # source: https://www.geeksforgeeks.org/sarsa-reinforcement-learning/
    def update(self, state, state2, reward, action, action2):
        predict = self.Q[state, action]
        target = reward + self.gamma * Q[state2, action2]
        self.Q[state, action] = self.Q[state, action] + self.alpha * (target - predict)
    
    def step(self, current_state, current_action):
        # Updating the new state, the reward for the step, whether pacman is done or not
        # perform the action a to get to the next state (s')
        new_state = self.update_game_state(current_state, current_action) # s'

        # get reward (r) for moving to the next state (s')
        reward = self.get_reward(current_state, current_action, new_state)
        
        # get action_greedy a' based on the next state s'
        action_greedy = self.get_action_greedy(new_state) # a'
        
        # find Q(s', a')
        self.update_Q(new_state, action_greedy)

        return new_state, action_greedy, reward
    
    def train(self, initial_state, initial_action):
        current_state = initial_state
        current_action = initial_action
        # call step unless is done or is dead
        while not (self.is_done() or self.is_dead()):
            new_state, action_greedy, reward = self.step(current_state, current_action)
            current_state = new_state
            current_action = action_greedy
    
    def get_action_epsilon(self, state):
        # will call get action random or get action greedy depending on epsilon
        action = 0
        if np.random.uniform(0, 1) < self.eps:
            action = self.get_action_random()
        else:
            action = self.get_action_greedy()
        return action
    
    def calculate_reward(state1, state2, action):
        return 0
    
    def action_to_command():
        return
    
    def evaluate():
        return

    