import asyncio
import os
import sys

import numpy as np
import torch

from gameState import GameState, GameModes, Directions
from time import time
import low_level
from low_level import send_direction, unstuck

# Path to the curc-pacbot-rl model definitions
_RL_SRC = os.path.join(os.path.dirname(os.path.abspath(__file__)), '../../curc-pacbot-rl/src')
sys.path.insert(0, _RL_SRC)
import models as _dqn_models  # noqa: E402

# Observation / action constants (from pacbot_rs_2/variables.rs and game_modes.rs)
OBS_SHAPE = torch.Size([17, 28, 31])
NUM_ACTIONS = 5
GHOST_FRIGHT_STEPS = 40
COMBO_MULTIPLIER = 200
PELLET_POINTS = 10
SUPER_PELLET_POINTS = 50
FRUIT_POINTS = 100
CHASE_DURATION = 180  # GameMode::CHASE.duration()

SUPER_PELLET_ROWS = frozenset({3, 23})
SUPER_PELLET_COLS = frozenset({1, 26})

# DQN action index → Pacbot Directions enum
# Rust Action enum: Stay=0, Down=1, Up=2, Left=3, Right=4
_ACTION_TO_DIR = [
    Directions.NONE,   # 0: Stay
    Directions.DOWN,   # 1: Down
    Directions.UP,     # 2: Up
    Directions.LEFT,   # 3: Left
    Directions.RIGHT,  # 4: Right
]


HYBRID_GHOST_RADIUS = 3


class DQNDecisionModule:
    '''
    Decision module that uses a pretrained DQN to select Pacbot actions.
    Drop-in replacement for DecisionModule — exposes the same decisionLoop().
    '''

    def __init__(self, state: GameState, checkpoint_path: str, log: bool = False,
                 hybrid_mode: bool = False, force_no_bot: bool = False) -> None:
        self.state = state
        self.log = log
        self.hybrid_mode = hybrid_mode
        self.force_no_bot = force_no_bot
        self.device = torch.device('cuda' if torch.cuda.is_available() else 'cpu')
        self._last_ghost_pos: list[tuple[int, int]] = [(32, 32)] * 4
        self._using_astar: bool = False
        self._load_model(checkpoint_path)

        if hybrid_mode:
            from decisionModule import DecisionModule
            self._astar = DecisionModule(state, log, hybrid_mode=True)
            print('[DQN] Hybrid mode enabled: falls back to A* when ghost within '
                  f'{HYBRID_GHOST_RADIUS} tiles')

    # ------------------------------------------------------------------
    # Model loading
    # ------------------------------------------------------------------

    def _load_model(self, checkpoint_path: str) -> None:
        loaded = torch.load(checkpoint_path, map_location='cpu', weights_only=False)
        model_name: str = loaded.get('config', {}).get('model', 'QNetV2')
        model_class = getattr(_dqn_models, model_name)
        self.q_net = model_class(OBS_SHAPE, NUM_ACTIONS).to(self.device)
        self.q_net.load_state_dict(loaded['state_dict'])
        self.q_net.eval()
        print(f'[DQN] Loaded {model_name} (iter={loaded.get("iter_num","?")}) on {self.device}')

    # ------------------------------------------------------------------
    # Observation construction
    # Matches the 17-channel (channels, col, obs_row) format used during
    # training, where obs_row = 30 - row (y-flipped from game coordinates).
    # ------------------------------------------------------------------

    def _build_obs(self) -> torch.Tensor:
        g = self.state
        obs = np.zeros((17, 28, 31), dtype=np.float32)

        # Channels 0 (walls) and 1 (rewards)
        for row in range(31):
            obs_row = 30 - row
            for col in range(28):
                if g.wallAt(row, col):
                    obs[0, col, obs_row] = 1.0
                if g.pelletAt(row, col):
                    is_super = row in SUPER_PELLET_ROWS and col in SUPER_PELLET_COLS
                    pts = SUPER_PELLET_POINTS if is_super else PELLET_POINTS
                    obs[1, col, obs_row] = pts / COMBO_MULTIPLIER
                elif g.fruitSteps > 0 and g.fruitLoc.row == row and g.fruitLoc.col == col:
                    obs[1, col, obs_row] = FRUIT_POINTS / COMBO_MULTIPLIER

        # Channels 2-3: Pacman (last pos reused as current; no tracking overhead)
        pr, pc = g.pacmanLoc.row, g.pacmanLoc.col
        if 0 <= pr < 31 and 0 <= pc < 28:
            obs[2, pc, 30 - pr] = 1.0
            obs[3, pc, 30 - pr] = 1.0

        # Channels 4-7: ghost current positions
        # Channels 8-11: ghost last positions (from previous step)
        # Channels 12-14: mode state at ghost location
        for i, ghost in enumerate(g.ghosts):
            gr, gc = ghost.location.row, ghost.location.col
            if 0 <= gr < 31 and 0 <= gc < 28:
                gobs = 30 - gr
                obs[4 + i, gc, gobs] = 1.0
                if ghost.isFrightened():
                    obs[14, gc, gobs] = ghost.frightSteps / GHOST_FRIGHT_STEPS
                else:
                    progress = g.modeSteps / CHASE_DURATION
                    ch = 13 if g.gameMode == GameModes.CHASE else 12
                    obs[ch, gc, gobs] = progress

            lgr, lgc = self._last_ghost_pos[i]
            if 0 <= lgr < 31 and 0 <= lgc < 28:
                obs[8 + i, lgc, 30 - lgr] = 1.0

        # Channel 15: ticks_per_step / update_period (1.0 approximation)
        obs[15, :, :] = 1.0

        # Channel 16: super pellet locations
        for row in SUPER_PELLET_ROWS:
            for col in SUPER_PELLET_COLS:
                if g.pelletAt(row, col):
                    obs[16, col, 30 - row] = 1.0

        return torch.from_numpy(obs).unsqueeze(0).to(self.device)  # (1,17,28,31)

    # ------------------------------------------------------------------
    # Action mask: True = action is physically reachable
    # ------------------------------------------------------------------

    def _get_action_mask(self) -> torch.Tensor:
        g = self.state
        row, col = g.pacmanLoc.row, g.pacmanLoc.col
        # Order matches Action enum: Stay, Down, Up, Left, Right
        deltas = [(0, 0), (1, 0), (-1, 0), (0, -1), (0, 1)]
        mask = [True] + [not g.wallAt(row + dr, col + dc) for dr, dc in deltas[1:]]
        return torch.tensor(mask, dtype=torch.bool).unsqueeze(0).to(self.device)

    # ------------------------------------------------------------------
    # Hybrid-mode helper
    # ------------------------------------------------------------------

    SUPER_PELLET_CHASE_RADIUS = 20

    def _ghost_within_radius(self) -> bool:
        pr, pc = self.state.pacmanLoc.row, self.state.pacmanLoc.col
        for ghost in self.state.ghosts:
            if ghost.isFrightened():
                continue
            gr, gc = ghost.location.row, ghost.location.col
            if abs(pr - gr) + abs(pc - gc) <= HYBRID_GHOST_RADIUS:
                return True
        return False

    def _astar_targeting_nearby_super_pellet(self) -> bool:
        """Return True if A*'s current destination is a super pellet within 20 tiles."""
        dest = getattr(self._astar, 'destination', None)
        if dest is None:
            return False
        dr, dc = dest
        if dr not in SUPER_PELLET_ROWS or dc not in SUPER_PELLET_COLS:
            return False
        if not self.state.pelletAt(dr, dc):
            return False
        pr, pc = self.state.pacmanLoc.row, self.state.pacmanLoc.col
        return abs(pr - dr) + abs(pc - dc) <= self.SUPER_PELLET_CHASE_RADIUS

    # ------------------------------------------------------------------
    # Inference
    # ------------------------------------------------------------------

    def get_direction(self) -> Directions:
        if self.hybrid_mode and (self._ghost_within_radius() or
                                  (self._using_astar and self._astar_targeting_nearby_super_pellet())):
            if not self._using_astar:
                self._using_astar = True
                print('[DQN] Hybrid: switching to A* (ghost nearby)')
            return self._astar.get_direction()

        if self.hybrid_mode and self._using_astar:
            self._using_astar = False
            print('[DQN] Hybrid: switching to RL (ghost far, no nearby super pellet target)')

        obs = self._build_obs()
        mask = self._get_action_mask()

        with torch.no_grad():
            q_vals = self.q_net(obs)        # (1, 5)
            q_vals[~mask] = -float('inf')
            action = int(q_vals.argmax(dim=1).item())

        # Record ghost positions for the next step's last-position channels
        for i, ghost in enumerate(self.state.ghosts):
            self._last_ghost_pos[i] = (ghost.location.row, ghost.location.col)

        return _ACTION_TO_DIR[action]

    # ------------------------------------------------------------------
    # Main async loop (same interface as DecisionModule.decisionLoop)
    #
    # Two modes depending on low_level.connected:
    #   connected     — act once per server state update (gate on currTicks)
    #   not connected — 3 pacbot moves per 2 ghost moves; ghosts move every
    #                   12 ticks at 24 fps = 0.5 s, so sleep 1/3 s per move
    #
    # Calls send_direction only when the output direction changes.
    # ------------------------------------------------------------------

    async def decisionLoop(self) -> None:
        last_ticks = -1
        last_direction = Directions.NONE
        stuck_pos = None
        stuck_start = None
        unstuck_triggered = False

        while self.state.isConnected():
            if low_level.connected:
                # Physical robot: act once per server state update
                await asyncio.sleep(0)
                if self.state.currTicks == last_ticks:
                    continue
                last_ticks = self.state.currTicks
            else:
                # Simulation: 3 pacbot moves per 2 ghost moves = 1 move per 1/3 s
                await asyncio.sleep(1 / 3)
                last_ticks = self.state.currTicks

            if self.state.gameMode == GameModes.PAUSED:
                continue

            if len(self.state.writeServerBuf):
                continue

            self.state.lock()

            direction = self.get_direction()
            pacman_pos = (self.state.pacmanLoc.row, self.state.pacmanLoc.col)

            if self.log:
                print(f'[DQN] pos=({self.state.pacmanLoc.row},{self.state.pacmanLoc.col}) '
                      f'dir={direction.name}')

            if direction != Directions.NONE:
                self.state.queueAction(1, direction)

            self.state.unlock()

            if direction != Directions.NONE:
                if pacman_pos != stuck_pos:
                    unstuck_triggered = False
                    stuck_pos = pacman_pos
                    stuck_start = time()
                elif not unstuck_triggered and (time() - stuck_start) >= 2.0:
                    unstuck_triggered = True
                    if not self.force_no_bot:
                        await unstuck(self.state, stuck_pos)
            else:
                stuck_pos = None
                stuck_start = None
                unstuck_triggered = False

            if direction != last_direction:
                if not self.force_no_bot:
                    send_direction(direction)
                last_direction = direction
