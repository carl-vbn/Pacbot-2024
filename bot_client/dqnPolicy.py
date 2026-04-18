from pathlib import Path
import sys

import numpy as np
import torch

from gameState import Directions, GameModes


OBS_SHAPE = (17, 28, 31)
NUM_ACTIONS = 5

PELLET_POINTS = 10
SUPER_PELLET_POINTS = 50
FRUIT_POINTS = 100
COMBO_MULTIPLIER = 200
LAST_REWARD = 3000
GHOST_FRIGHT_STEPS = 40
CHASE_DURATION = 180


ACTION_TO_DIRECTION = {
	0: Directions.NONE,
	1: Directions.DOWN,
	2: Directions.UP,
	3: Directions.LEFT,
	4: Directions.RIGHT,
}


def _repo_root() -> Path:
	return Path(__file__).resolve().parents[2]


def _rl_src_path() -> Path:
	return _repo_root() / "curc-pacbot-rl" / "src"


def _resolve_checkpoint(checkpoint: str) -> Path:
	path = Path(checkpoint).expanduser()
	if path.is_absolute() and path.exists():
		return path

	candidates = [
		Path.cwd() / path,
		Path(__file__).resolve().parent / path,
		_repo_root() / path,
		_rl_src_path() / path,
	]
	for candidate in candidates:
		if candidate.exists():
			return candidate.resolve()

	raise FileNotFoundError(f"DQN checkpoint not found: {checkpoint}")


def _select_device(preferred: str | None) -> torch.device:
	if preferred:
		return torch.device(preferred)
	if getattr(torch.backends, "mps", None) is not None and torch.backends.mps.is_available():
		return torch.device("mps")
	if torch.cuda.is_available():
		return torch.device("cuda")
	return torch.device("cpu")


class DQNPolicy:
	def __init__(
		self,
		checkpoint: str,
		device: str | None = None,
		log: bool = False,
		super_checkpoint: str | None = "checkpoints_known/superpelletmode.safetensors",
	) -> None:
		rl_src = _rl_src_path()
		if str(rl_src) not in sys.path:
			sys.path.insert(0, str(rl_src))

		import models

		self.log = log
		self.device = _select_device(device)
		self.checkpoint = _resolve_checkpoint(checkpoint)
		self.q_net, self.config = self._load_q_net(models, self.checkpoint)

		self.super_checkpoint = _resolve_checkpoint(super_checkpoint) if super_checkpoint else None
		self.super_q_net = None
		if self.super_checkpoint is not None:
			self.super_q_net, _ = self._load_q_net(models, self.super_checkpoint)

		print(f"Loaded DQN policy from {self.checkpoint} on {self.device}")
		if self.super_checkpoint is not None:
			print(f"Loaded DQN super-pellet policy from {self.super_checkpoint} on {self.device}")

	def _load_q_net(self, models, checkpoint: Path):
		if checkpoint.suffix == ".safetensors":
			import safetensors.torch

			q_net = models.QNetV2(torch.Size(OBS_SHAPE), NUM_ACTIONS).to(self.device)
			q_net.load_state_dict(safetensors.torch.load_file(str(checkpoint)))
			config = {"model": "QNetV2"}
		else:
			loaded = torch.load(checkpoint, map_location="cpu", weights_only=False)
			model_class = getattr(models, loaded["config"]["model"])
			q_net = model_class(torch.Size(OBS_SHAPE), NUM_ACTIONS).to(self.device)
			q_net.load_state_dict(loaded["state_dict"])
			config = loaded.get("config", {})

		q_net.eval()
		return q_net, config

	def choose_direction(self, state) -> Directions:
		use_super_policy = self.super_q_net is not None and (
			self._are_ghosts_close(state) or not self._all_ghosts_not_frightened(state)
		)
		q_net = self.super_q_net if use_super_policy else self.q_net
		obs = torch.from_numpy(
			self._obs_numpy(state, obs_ignore_super_pellets=not use_super_policy)
		).to(self.device).unsqueeze(0)
		action_mask = torch.tensor(
			self._action_mask(state, avoid_super_pellets=not use_super_policy),
			device=self.device,
			dtype=torch.bool,
		).unsqueeze(0)

		with torch.no_grad():
			q_values = q_net(obs)
			q_values[~action_mask] = -torch.inf
			action = int(q_values.argmax(dim=1).item())

		if self.log:
			print(f"DQN q-values: {q_values.squeeze(0).detach().cpu().tolist()}, action: {action}")

		return ACTION_TO_DIRECTION[action]

	def _action_mask(self, state, avoid_super_pellets: bool) -> list[bool]:
		row = state.pacmanLoc.row
		col = state.pacmanLoc.col
		candidates = [
			(row, col),
			(row + 1, col),
			(row - 1, col),
			(row, col - 1),
			(row, col + 1),
		]
		mask = [
			True,
			not state.wallAt(row + 1, col),
			not state.wallAt(row - 1, col),
			not state.wallAt(row, col - 1),
			not state.wallAt(row, col + 1),
		]
		if avoid_super_pellets:
			for action, (target_row, target_col) in enumerate(candidates):
				if action != 0 and self._is_super_pellet(target_row, target_col):
					mask[action] = False
		return mask

	def _obs_numpy(self, state, obs_ignore_super_pellets: bool) -> np.ndarray:
		obs = np.zeros(OBS_SHAPE, dtype=np.float32)
		num_pellets = state.numPellets()

		for row in range(31):
			obs_row = 31 - row - 1
			for col in range(28):
				obs[0, col, obs_row] = float(state.wallAt(row, col))

				if state.pelletAt(row, col):
					is_super = ((row == 3) or (row == 23)) and ((col == 1) or (col == 26))
					if not (is_super and obs_ignore_super_pellets):
						points = SUPER_PELLET_POINTS if is_super else PELLET_POINTS
						if num_pellets == 1:
							points += LAST_REWARD
						obs[1, col, obs_row] = points / COMBO_MULTIPLIER

					if is_super and not obs_ignore_super_pellets:
						obs[16, col, obs_row] = 1.0
				elif state.fruitAt(row, col):
					obs[1, col, obs_row] = FRUIT_POINTS / COMBO_MULTIPLIER

		self._mark_location(obs, 2, state.pacmanLoc)
		self._mark_location(obs, 3, state.pacmanLoc)

		for ghost_index, ghost in enumerate(state.ghosts):
			pos = self._location_to_obs_pos(ghost.location)
			if pos is None:
				continue
			col, obs_row = pos
			obs[4 + ghost_index, col, obs_row] = 1.0
			obs[8 + ghost_index, col, obs_row] = 1.0

			if ghost.isFrightened():
				obs[14, col, obs_row] = ghost.frightSteps / GHOST_FRIGHT_STEPS
				obs[1, col, obs_row] += float(2 ** state.ghostCombo)
			else:
				state_channel = 13 if state.gameMode == GameModes.CHASE else 12
				obs[state_channel, col, obs_row] = state.modeSteps / CHASE_DURATION

		update_period = state.updatePeriod if state.updatePeriod else 12
		obs[15, :, :] = 8.0 / update_period
		return obs

	def _mark_location(self, obs: np.ndarray, channel: int, location) -> None:
		pos = self._location_to_obs_pos(location)
		if pos is None:
			return
		col, obs_row = pos
		obs[channel, col, obs_row] = 1.0

	def _location_to_obs_pos(self, location) -> tuple[int, int] | None:
		if (location.row < 0 or location.row >= 31) or (location.col < 0 or location.col >= 28):
			return None
		return location.col, 31 - location.row - 1

	def _are_ghosts_close(self, state) -> bool:
		total_distance = 0
		pacman_row = state.pacmanLoc.row
		pacman_col = state.pacmanLoc.col
		for ghost in state.ghosts:
			total_distance += abs(pacman_row - ghost.location.row) + abs(pacman_col - ghost.location.col)
		return total_distance < 30

	def _all_ghosts_not_frightened(self, state) -> bool:
		return all(not ghost.isFrightened() for ghost in state.ghosts)

	def _is_super_pellet(self, row: int, col: int) -> bool:
		return ((row == 3) or (row == 23)) and ((col == 1) or (col == 26))
