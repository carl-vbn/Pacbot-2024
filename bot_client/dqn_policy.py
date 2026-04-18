import torch
import numpy as np
import os
import sys

# Add curc-pacbot-rl/src to sys.path to import models
# Assuming the directory structure:
# /Users/nassa/Downloads/Robotics/
#   Pacbot-2024/bot_client/
#   curc-pacbot-rl/src/
script_dir = os.path.dirname(os.path.abspath(__file__))
rl_src_dir = os.path.abspath(os.path.join(script_dir, "../../curc-pacbot-rl/src"))
if rl_src_dir not in sys.path:
    sys.path.append(rl_src_dir)

import models
from gameState import GameState, GameModes, GhostColors, Directions

class DQNPolicy:
    def __init__(self, checkpoint_path, device='cpu'):
        self.device = torch.device(device)
        self.load_checkpoint(checkpoint_path)
        self.last_pacman_pos = None
        self.last_ghost_pos = [None] * 4

    def load_checkpoint(self, ckpt_path):
        # We need to handle the case where models might not be available if sys.path is wrong
        # But we added it above.
        loaded = torch.load(ckpt_path, map_location=self.device, weights_only=False)
        model_name = loaded['config'].get('model', 'QNetV2')
        model_class = getattr(models, model_name)
        
        # Observation shape is fixed (17, 28, 31)
        self.obs_shape = (17, 28, 31)
        self.num_actions = 5 # Stay, Down, Up, Left, Right
        
        self.q_net = model_class(self.obs_shape, self.num_actions).to(self.device)
        self.q_net.load_state_dict(loaded['state_dict'])
        self.q_net.eval()
        print(f"Loaded DQN model: {model_name} from {ckpt_path}")

    def reset(self):
        self.last_pacman_pos = None
        self.last_ghost_pos = [None] * 4

    def get_observation(self, state: GameState):
        obs = np.zeros(self.obs_shape, dtype=np.float32)
        
        # Constants from curc-pacbot-rl
        PELLET_POINTS = 10
        SUPER_PELLET_POINTS = 50
        FRUIT_POINTS = 100
        COMBO_MULTIPLIER = 10
        GHOST_FRIGHT_STEPS = 40 # Approximation, GameState uses 40
        CHASE_DURATION = 180 # Approximation
        
        # 0: wall
        # 1: reward
        for row in range(31):
            for col in range(28):
                obs_row = 31 - row - 1
                if state.wallAt(row, col):
                    obs[0, col, obs_row] = 1.0
                
                reward = 0
                if state.pelletAt(row, col):
                    if state.superPelletAt(row, col):
                        reward = SUPER_PELLET_POINTS
                    else:
                        reward = PELLET_POINTS
                    # Check if it's the last pellet
                    if state.numPellets() == 1:
                        reward += 3000
                elif state.fruitAt(row, col):
                    reward = FRUIT_POINTS
                
                obs[1, col, obs_row] = reward / COMBO_MULTIPLIER

        # 2-3: pacman
        curr_pacman_pos = (state.pacmanLoc.col, 31 - state.pacmanLoc.row - 1)
        if self.last_pacman_pos is None:
            self.last_pacman_pos = curr_pacman_pos
        
        if 0 <= self.last_pacman_pos[0] < 28 and 0 <= self.last_pacman_pos[1] < 31:
            obs[2, self.last_pacman_pos[0], self.last_pacman_pos[1]] = 1.0
        if 0 <= curr_pacman_pos[0] < 28 and 0 <= curr_pacman_pos[1] < 31:
            obs[3, curr_pacman_pos[0], curr_pacman_pos[1]] = 1.0
        self.last_pacman_pos = curr_pacman_pos

        # 4-7: ghosts, 8-11: last ghosts, 12-14: ghost states
        for i, ghost in enumerate(state.ghosts):
            col = ghost.location.col
            row = ghost.location.row
            if row < 31 and col < 28:
                obs_row = 31 - row - 1
                obs[4 + i, col, obs_row] = 1.0
                
                if ghost.isFrightened():
                    obs[14, col, obs_row] = ghost.frightSteps / GHOST_FRIGHT_STEPS
                    obs[1, col, obs_row] += (2 ** state.ghostCombo) / COMBO_MULTIPLIER
                else:
                    mode_idx = 13 if state.gameMode == GameModes.CHASE else 12
                    # state.modeSteps is used for both scatter and chase
                    obs[mode_idx, col, obs_row] = state.modeSteps / CHASE_DURATION

                # Last ghost pos
                last_pos = self.last_ghost_pos[i]
                if last_pos is None:
                    last_pos = (col, obs_row)
                if 0 <= last_pos[0] < 28 and 0 <= last_pos[1] < 31:
                    obs[8 + i, last_pos[0], last_pos[1]] = 1.0
                self.last_ghost_pos[i] = (col, obs_row)

        # 15: ticks per step / update period
        obs[15, :, :] = 8.0 / state.updatePeriod # Assuming 8 ticks per step as in env.rs

        # 16: super pellet map
        for row in [3, 23]:
            for col in [1, 26]:
                if state.pelletAt(row, col):
                    obs[16, col, 31 - row - 1] = 1.0
                    
        return obs

    def select_action(self, state: GameState):
        obs = self.get_observation(state)
        obs_tensor = torch.from_numpy(obs).unsqueeze(0).to(self.device)
        
        # Action mask
        mask = [True] * 5
        p = state.pacmanLoc
        mask[0] = True # Stay
        mask[1] = not state.wallAt(p.row + 1, p.col) # Down
        mask[2] = not state.wallAt(p.row - 1, p.col) # Up
        mask[3] = not state.wallAt(p.row, p.col - 1) # Left
        mask[4] = not state.wallAt(p.row, p.col + 1) # Right
        
        mask_tensor = torch.tensor(mask, device=self.device).unsqueeze(0)
        
        with torch.no_grad():
            q_values = self.q_net(obs_tensor)
            # Apply mask by setting masked actions to a very small value
            q_values[~mask_tensor] = -float('inf')
            action_idx = torch.argmax(q_values, dim=1).item()
            
        # Map action_idx to Directions
        # env.rs Action: Stay=0, Down=1, Up=2, Left=3, Right=4
        # GameState Directions: UP=0, LEFT=1, DOWN=2, RIGHT=3, NONE=4
        
        mapping = {
            0: Directions.NONE,  # Stay
            1: Directions.DOWN,
            2: Directions.UP,
            3: Directions.LEFT,
            4: Directions.RIGHT
        }
        return mapping[action_idx]
