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

// Rotational alignment sequence
int target_dist_sum; // Sum of left and right sensor values when aligned with walls
bool rot_dir;
long rot_end_time;
int dist_err_prerot;

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

  rot_dir = false;
  rot_end_time = 0;
}

#define STOP() bot_state.stopped = true
#define START() bot_state.stopped = false
#define SET_SPEED(speed) bot_state.speed = (speed)
#define SET_DIR(_dir) bot_state.dir = (_dir)
#define PAUSE(duration) bot_state.pause_time += (duration)
#define TURN_RIGHT() bot_state.dir = (bot_state.dir + 1) % 4
#define TURN_LEFT() bot_state.dir = (bot_state.dir + 3) % 4

void movement_tick(long delta_time, int gpioVal) {
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

  // if (frontDist < OPTIMAL_FRONT_DIST) {
  //   MOVE_SIDE(BACK, BACKOFF_SPEED, 0);
  // } else {
  //   MOVE_SIDE(FRONT, frontDist < 255 ? DANGER_SPEED : BASE_SPEED, rightBias);
  // }

  // Align in the maze
  long now = millis();
  int current_dist_sum = rightDist + leftDist;
  int dist_sum_error = abs(current_dist_sum - target_dist_sum);
  if (gpioVal == 2) {
    // Compute target_dist_sum
    target_dist_sum = current_dist_sum;
  }

  if (gpioVal == 1) {
    if (dist_sum_error > 10) {
      // Rotational alignment
      if (now > rot_end_time) {
        if (dist_err_prerot < dist_sum_error)
          rot_dir = !rot_dir;
        
        dist_err_prerot = dist_sum_error;
        rot_end_time = now + 50;
      }
      if (rot_dir) m_clockwise(70);
      else m_counterclockwise(70);
    } else if (abs(rightShift) > 10) {
      // Lateral alignment
      if (rightShift > 0) {
        MOVE_SIDE(LEFT, 70, 0);
      } else {
        MOVE_SIDE(RIGHT, 70, 0);
      }
    } else {
      m_stop();
    }
  }
}