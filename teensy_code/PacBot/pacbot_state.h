// Config
#define BASE_SPEED 120
#define DANGER_SPEED 80 // Danger means an obstacle is detected ahead
#define BACKOFF_SPEED 80 // Speed while reversing
#define MIN_FRONT_DIST 7
#define MAX_FRONT_DIST 15
#define LATERAL_CORRECTION_SPEED 70
#define ROTATIONAL_CORRECTION_SPEED 80

typedef struct {
  uint8_t dir;
  bool stopped;
  long pause_time;
  long backoff_time;
  bool wall_detected;
  unsigned long playStartTime;
  int startDir;
} pb_state_t;

pb_state_t bot_state;

// Rotational alignment sequence
int target_dist_sum; // Sum of left and right sensor values when aligned with walls
bool rot_dir;
long rot_end_time;
int dist_err_prerot;
int target_right_dist;
int target_left_dist;
long recovery_counter;
uint8_t lastRightDist;
uint8_t lastLeftDist;
bool leftVoid;
bool rightVoid;

#define READ_SIDE_DIST(side) (sensors[(bot_state.dir + (side)) % 4].readRangeContinuous())
#define MOVE_DIR(dir, speed, rightBias) m_funcs[(dir)]((speed), (rightBias));
#define MOVE_SIDE(side, speed, rightBias) MOVE_DIR((bot_state.dir + (side)) % 4, speed, rightBias)

typedef void (*motor_func_t)(int, int);
const motor_func_t m_funcs[4] = {m_north, m_east, m_south, m_west};

void calibrate() {
  uint8_t backDist = READ_SIDE_DIST(BACK);
  uint8_t frontDist = READ_SIDE_DIST(FRONT);
  uint8_t leftDist = READ_SIDE_DIST(LEFT);
  uint8_t rightDist = READ_SIDE_DIST(RIGHT);
  lastRightDist = rightDist;
  lastLeftDist = leftDist;
  int current_dist_sum = rightDist + leftDist;

  target_dist_sum = current_dist_sum;
  target_right_dist = rightDist;
  target_left_dist = leftDist;

  bot_state.playStartTime = 0;
  bot_state.startDir = 0;

  //calibrate_imu();
}

void init_state() {
  bot_state.dir = NORTH;
  bot_state.stopped = true;
  bot_state.pause_time = 0;
  bot_state.backoff_time = 0;
  bot_state.wall_detected = false;

  rot_dir = false;
  rot_end_time = 0;
  recovery_counter = 0;
  rightVoid = false;
  leftVoid = false;

  bot_state.playStartTime = millis();

  calibrate();
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
  int current_dist_sum = rightDist + leftDist;
  int rightShift = (leftDist - target_left_dist) + (target_right_dist - rightDist);
  // bool rightVoid = abs(target_right_dist - rightDist) >= 255;
  // bool leftVoid = abs(leftDist - target_left_dist) >= 255;

  int16_t dRightDist = rightDist - lastRightDist;
  int16_t dLeftDist = leftDist - lastLeftDist;
  lastRightDist = rightDist;
  lastLeftDist = leftDist;

  if (dRightDist > 40) {
    rightVoid = true;
  } else if (dRightDist < -40) {
    rightVoid = false;
  }

  if (dLeftDist > 40) {
    leftVoid = true;
  } else if (dLeftDist < -40) {
    leftVoid = false;
  }

  // Serial.print(rightVoid);
  // Serial.println(leftVoid);

  bool anyVoid = leftVoid || rightVoid;
  bool allVoid = rightVoid && leftVoid && frontDist > 250 && backDist > 250;

  // if (frontDist < 255 && !bot_state.wall_detected) {
  //   PAUSE(200);
  //   bot_state.wall_detected = true;
  // }

  if (frontDist >= 255 && bot_state.wall_detected) {
    bot_state.wall_detected = false;
  }

  if (bot_state.stopped) {
    m_stop();
    return;
  }

  if (bot_state.pause_time > 0) {
    m_stop();
    bot_state.pause_time -= delta_time;

    if (bot_state.pause_time < 0) {
      bot_state.pause_time = 0;
    }

    return;
  }

  if (bot_state.backoff_time > 0) {
    // MOVE_SIDE(BACK, BACKOFF_SPEED, 0);
    m_compound(bot_state.dir, -BACKOFF_SPEED, 0, 0);
    bot_state.backoff_time -= delta_time;

    if (bot_state.backoff_time < 0) {
      bot_state.backoff_time = 0;
      PAUSE(300);
    }

    return;
  }

  int frontSpeed = 0;
  int lateralSpeed = 0;
  int rotation = 0;

  if (frontDist < MIN_FRONT_DIST) {
    // frontSpeed = -BACKOFF_SPEED; 
    bot_state.backoff_time += 70;
  } else if (frontDist > MAX_FRONT_DIST) {
    frontSpeed = frontDist < 255 ? DANGER_SPEED : BASE_SPEED;
  }

  // Alignment corrections
  long now = millis();

  int dist_sum_error = abs(current_dist_sum - target_dist_sum);
  // Serial.print("dse ");
  // Serial.println(dist_sum_error);
  //int yaw = READ_YAW();
  if (frontSpeed >= 0 && !rightVoid && !leftVoid) {
    if (dist_sum_error > 12) {
      // Rotational alignment
      frontSpeed = 0;
      if (now > rot_end_time) {
        if (dist_err_prerot < dist_sum_error)
          rot_dir = !rot_dir;
        
        dist_err_prerot = dist_sum_error;
        rot_end_time = now + 15;
      }
      if (rot_dir) rotation = ROTATIONAL_CORRECTION_SPEED;
      else rotation = -ROTATIONAL_CORRECTION_SPEED;
      // if (yaw > 0) {
      //   rotation = -ROTATIONAL_CORRECTION_SPEED;
      // } else {
      //   rotation = ROTATIONAL_CORRECTION_SPEED;
      // }
    } else if (abs(rightShift) > 10) {
      // Lateral alignment
      if (rightShift > 0) {
        lateralSpeed = -LATERAL_CORRECTION_SPEED;
      } else {
        lateralSpeed = LATERAL_CORRECTION_SPEED;
      }
    }
  }
  
  // Serial.print(frontSpeed);
  // Serial.print(" ");
  // Serial.print(lateralSpeed);
  // Serial.print(" ");
  // Serial.println(rotation);

  m_compound(bot_state.dir, frontSpeed, lateralSpeed, rotation);
}

void recovery(long delta_time) {
  recovery_counter += delta_time;
  int recoveryMode = recovery_counter / 500;

  // if (recoveryMode <= 1) {
  //   m_east(230, 0);
  //   return;
  // }

  // if (recoveryMode == 0) {
  //   m_north(120, 0);
  // } else if (recoveryMode == 1) {
  //   m_south(120, 0);
  // } else if (recoveryMode == 2) {
  //   m_east(120, 0);
  // } else if (recoveryMode == 3) {
  //   m_west(120, 0);
  // } else {
  //   recovery_counter = 0;
  // }
  randomSeed(recoveryMode);
  if (recoveryMode <= 4 || recoveryMode % 2 == 0)
    m_compound(random(0,4), 180, 0, 0);
  else
    m_compound(0, 0, 0, random(0,100) < 50 ? 120 : -120);
}