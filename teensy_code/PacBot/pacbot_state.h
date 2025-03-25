// Directions (relative to the maze)
#define NORTH 0
#define EAST  1
#define SOUTH 2
#define WEST  3

// Sides (relative to the robot)
#define FRONT 0
#define RIGHT 1
#define BACK  2
#define LEFT  3

// Config
#define BASE_SPEED 150
#define DANGER_SPEED 80 // Danger means an obstacle is detected ahead
#define BACKOFF_SPEED 80 // Speed while reversing
#define OPTIMAL_FRONT_DIST 50

typedef struct {
  uint8_t dir;
  bool stopped;
  long pause_time;
} pb_state_t;

pb_state_t bot_state;

#define READ_DIR_DIST(dir) (sensors[(dir)].readRangeContinuous())
#define READ_SIDE_DIST(side) (sensors[(bot_state.dir + (side)) % 4].readRangeContinuous())
#define MOVE_DIR(dir, speed, rightBias) m_funcs[(dir)]((speed), (rightBias));
#define MOVE_SIDE(side, speed, rightBias) MOVE_DIR((bot_state.dir + (side)) % 4, speed, rightBias)

typedef void (*motor_func_t)(int, int);
const motor_func_t m_funcs[4] = {m_north, m_east, m_south, m_west};

void init_state() {
  bot_state.dir = NORTH;
  bot_state.stopped = true;
  bot_state.pause_time = 0;
}

#define STOP() bot_state.stopped = true
#define START() bot_state.stopped = false
#define SET_SPEED(speed) bot_state.speed = (speed)
#define PAUSE(duration) bot_state.pause_time += (duration)
#define TURN_RIGHT() bot_state.dir = (bot_state.dir + 1) % 4
#define TURN_LEFT() bot_state.dir = (bot_state.dir + 3) % 4

void movement_tick(long delta_time) {
  uint8_t backDist = READ_SIDE_DIST(BACK);
  uint8_t frontDist = READ_SIDE_DIST(FRONT);
  uint8_t leftDist = READ_SIDE_DIST(LEFT);
  uint8_t rightDist = READ_SIDE_DIST(RIGHT);
  int rightShift = leftDist - rightDist;
  int rightBias = 0;
  bool rightVoid = rightDist > 250;
  bool leftVoid = leftDist > 250;
  bool anyVoid = leftVoid || rightVoid;
  bool allVoid = rightVoid && leftVoid && frontDist > 250 && backDist > 250;

  if (bot_state.stopped) {
    m_stop();
    if (backDist < 100) START(); // Start the robot by putting your hand near the back
    return;
  } else if (allVoid) {
    STOP(); // Stop if lifted away from the maze
    m_stop();
    return;
  }

  if (bot_state.pause_time > 0) {
    m_stop();
    bot_state.pause_time -= delta_time;
    return;
  }

  if (!anyVoid && abs(rightShift) > 10) {
    if (rightShift > 0) {
      rightBias = 30;
    } else {
      rightBias = -30;
    }
  }

  if (frontDist < OPTIMAL_FRONT_DIST) {
    MOVE_SIDE(BACK, BACKOFF_SPEED, 0);
  } else {
    MOVE_SIDE(FRONT, frontDist < 255 ? DANGER_SPEED : BASE_SPEED, rightBias);
  }

  if (frontDist >= OPTIMAL_FRONT_DIST && frontDist < (OPTIMAL_FRONT_DIST + 5)) {
    if (rightVoid) {
      TURN_RIGHT();
    } else if (leftVoid) {
      TURN_LEFT();
    } else {
      STOP();
    }
  }
}