import math
from gameState import GameState
from debugServer import DebugServer
import pathfinding
import numpy as np

class RLLearn_SARAS():
    def __init__(self, epsilon=0.05,gamma=0.8,alpha=0.2, numTraining=0):

        # SARSA parameters for learning
        # default values from: http://ai.berkeley.edu/projects/release/reinforcement/v1/001/docs/qlearningAgents.html

        self.alpha = alpha # learning rate
        self.epsilon = epsilon # exploration rate
        self.gamma = gamma # discount factor
        self.numTraining = numTraining # number of training episodes, 0 for no training

        self.states = self.create_state_list() # state-action space
        
        # potentially helpful reference:
        # https://github.com/wrhlearner/PacBot-2023/blob/master/src/Pi/botCode/HighLevelMarkov.py


        # a dictionary for storing Q(s,a)
        # a list records last state
        # a list records last action
        # a variable stores the score before last action

    def create_state_list():
        """
        This function calculates all possible states,
        based on Pacman's position, the ghosts' positions, 
        the frightened state of the ghosts, whether the ghosts are alive, and the position of the pellets.

        Many states are inacessible, Pacman, pellets, and ghosts can't be in walls,
        but we can ignore that for now.

        28 * 31 (dimension of arena) = 868 possible positions for pacman.
        4*(28 * 31 * 2 + 1) = 6948 possible states for the 4 ghosts (position, frightened, or dead).
        28 * 31 = 868 possible positions for each pellet.
        add em all up: 868 + 6948 + 868 = 8684.

        Oh wait, these should actually be multiplied by one another...
        868 * 6948 * 868 = 5,234,789,952 possible states.

        Hmm, how about we only consider valid positions (aka. excluding walls)?
        There are 288 valid positions, so...
        288 * (4*(288*2+1)) * 288 = 191,434,752.

        Thus, 191,434,752 possible states, with 4 possible actions (up, down, left, right).

        We obviously can't store all of these in memory, so we need a less naive paradigm...

        Seems like the better approach is to use a CNN:
        https://cs229.stanford.edu/proj2017/final-reports/5241109.pdf
        """

        return np.full((191434752, 4), 0.5)
    
    def q_mapper(GameState):
        # figure out where a state is in the Q table
        #returns a dictionary mapping rewards to each state
        return

    
    def stepping(state, action):
        #Updating the new state, the reward for the step, whether pacman is done or not
        #should call 
        next_state = state
        reward = 0
        done = False
        return next_state, reward, done
    
    def get_possible_actions(state):
        #given a state, return a list of possible states
        return
    
    def get_action_random():
        #returns a random action from the state space
        return
        
    def get_action_greedy():
        return
        
    def get_action_epsilon():
        return
    
    def calculate_reward(state1, state2, action):
        #takes a board state, the next state and the action and calculates the ending rewards 
        return 0
    
    def action_to_command():
        return
    
    def train(train=True):
        
        # initialize Q(s,a)
        # take a random action
        # update Q(s,a)
        # choose the action maximises Q or a random action according to Ɛ-greedy function
        # repeat step 3 and 4 until the game ends
        # update Q(s,a) where s is the last state before the end, a is the last action taken
        #when you are at the last state, print out the statistics -> call evaluate()
        #if train is False, do not ever do e greedy, always choose the greedy actions

        return

    def evaluate(max_steps, episodes, train=True):
        #print out the average reward, what is the average reward for the episdoes, 
        #how many episdoes sucess, how many died
        #how many steps before death on average (maybe print the whole array)
        return  

    def update_values():
        #every time an episode ends, what is
        return

    