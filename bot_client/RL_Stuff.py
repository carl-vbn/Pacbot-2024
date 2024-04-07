import math
from gameState import GameState
from debugServer import DebugServer
import pathfinding
import numpy as np
import numpy as np

class RLLearn_SARAS():
    def __init__(self, addr, port):
        self.subscriptions = [MsgType.LIGHT_STATE]
        super().__init__(addr, port, message_buffers, MsgType, FREQUENCY, self.subscriptions)
        self.state = None
        self.grid = copy.deepcopy(grid)

        self.policy = np.zeros((grid.shape, 4))
        self.lr = 0.01
        self.min_lr = 0.001
        self.lr_decay = 0.99
        self.gamma = 1
        self.eps = 0.1
        self.eps_decay = 0.999
        self.min_eps = 0.001
        self.Q = np.zeros() # the policy
        self.gamma = 0
        self.alpha = 0
        return

    def is_done(self):
        done = True
        for i in self.grid.shape[0]:
            for j in self.grid.shape[1]:
                if self.grid[i][j] in [o, O]:
                    done = False
        return done
    
    def is_dead(self, state):
        return self.grid[state[0]][state[1]]
    
    #Function to learn the Q-value 
    # source: https://www.geeksforgeeks.org/sarsa-reinforcement-learning/
    def update(self, state, state2, reward, action, action2):
        predict = self.Q[state, action]
        target = reward + self.gamma * Q[state2, action2]
        self.Q[state, action] = self.Q[state, action] + self.alpha * (target - predict)
    
    def stepping(self, past_state, past_action):
        #Updating the new state, the reward for the step, whether pacman is done or not
        
        ## OLD CODE
        
        state = past_state
        eat_pellet = False
        if self.state and self.state.mode == LightState.RUNNING:
            state = (self.state.pacman.x, self.state.pacman.y)

        # update game state, we have eaten the little thing good for us
        if self.grid[state[0]][state[1]] in [o, O]:
            self.grid[state[0]][state[1]] = e
            eat_pellet = True
        
        
        ## GET THE NEXT ACTION BASED ON EPSILON
        action = self.get_action_epsilon(state, False)
        print(action) #action is equivilant of path[1], it should be of the form,
        command = self.action_to_command(action)
        print(command)

        done = self.is_done() #return true if eaten all coins
        dead = self.is_dead()
        reward = self.get_reward(action, state, done, past_action, eat_pellet, dead, past_state)
       
        if action != None:
            # Figure out position we need to move
            new_msg = PacmanCommand()
            new_msg.dir = self._get_direction(state, action)
            self.write(new_msg.SerializeToString(), MsgType.PACMAN_COMMAND)
            return

        new_msg = PacmanCommand()
        new_msg.dir = PacmanCommand.STOP
        self.write(new_msg.SerializeToString(), MsgType.PACMAN_COMMAND)



        return self.get_next_state(action), reward, done, dead
    
    def get_action_random():
        return
        
    def get_action_greedy():
        return
        
    def get_action_epsilon(self, state):
        # will call get action random or get action greedy depending on epsilon
        action = 0
        if np.random.uniform(0, 1) < self.eps:
            action = self.get_action_random()
        else:
            action = self.get_action_greedy()
        return action


    
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

    