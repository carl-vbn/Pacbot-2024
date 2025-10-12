// Config
#define BASE_SPEED 120
#define DANGER_SPEED 80 // Danger means an obstacle is detected ahead
#define BACKOFF_SPEED 80 // Speed while reversing
#define MIN_FRONT_DIST 7
#define MAX_FRONT_DIST 15
#define LATERAL_CORRECTION_SPEED 70
#define ROTATIONAL_CORRECTION_SPEED 80

// PID Tuning
#define Kp 3.0      // start here; tune on your robot
#define Ki 0.1     // small integral to remove steady-state bias
#define Kd 0.2      // derivative to dampen oscillation

// PID Loop
int basePWM = 140; // forward speed (0-255). Increase once heading is stable.
int maxPWM = 255; // safety clamp for motor commands
float maxI = 50;  // integral windup clamp (in "error*sec" units)
float maxTurn = 120; // clamp PID output used for turning correction (PWM units)

float targetYawDeg = 0.0;
float integ = 0.0;
float prevErr = 0.0;
unsigned long prevMs = 0;

typedef struct {
  uint8_t dir;
  bool stopped;
  long pause_time;
  long backoff_time;
  bool wall_detected;
  unsigned long playStartTime;
} pb_state_t;


pb_state_t bot_state;

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

  // target_right_dist = rightDist;
  // target_left_dist = leftDist;

  bot_state.playStartTime = 0;

  calibrate_imu();

  targetYawDeg = READ_YAW();
}

void init_state() {
  Serial.println("init_state");
  bot_state.dir = NORTH;
  bot_state.stopped = true;
  bot_state.pause_time = 0;
  bot_state.backoff_time = 0;
  bot_state.wall_detected = false;

  rightVoid = false;
  leftVoid = false;

  bot_state.playStartTime = millis();
  prevMs = millis();

  calibrate();
}

#define STOP() bot_state.stopped = true
#define START() bot_state.stopped = false
#define SET_SPEED(speed) bot_state.speed = (speed)
#define SET_DIR(_dir) bot_state.dir = (_dir)
#define PAUSE(duration) bot_state.pause_time += (duration)
#define TURN_RIGHT() bot_state.dir = (bot_state.dir + 1) % 4
#define TURN_LEFT() bot_state.dir = (bot_state.dir + 3) % 4

float angleDiffDeg(float d) {
  while (d > 180.0f) d -= 360.0f;
  while (d < -180.0f) d += 360.0f;
  return d;
}

int map_rotation_speed(float speed) {
  // Returns 0-1023

  float absSpeed = abs(speed);

  if (absSpeed < 50) {
    return 0;
  } else if (absSpeed > 200) {
    return 200;
  } else {
    return speed;
  }
}

void movement_tick(long delta_time, int gpioVal) {
  uint8_t backDist = READ_SIDE_DIST(BACK);
  uint8_t frontDist = READ_SIDE_DIST(FRONT);
  uint8_t leftDist = READ_SIDE_DIST(LEFT);
  uint8_t rightDist = READ_SIDE_DIST(RIGHT);

  int distanceDelta = rightDist - leftDist;

  bool lateralVoid = leftDist > 200 || rightDist > 200;

  imu_tick();

  if (bot_state.stopped) {
    m_stop();
    return;
  }

  int frontSpeed = 100;
  int lateralSpeed = 0;
  int rotation = 0;

  // Lateral correction
  if (!lateralVoid && abs(distanceDelta) > 30) {
    lateralSpeed = distanceDelta > 0 ? 80 : -80;
  }

  // PID Calculation (thanks gpt)

  unsigned long now = millis();
  float dt = (now - prevMs) / 1000.0f;
  if (dt <= 0) dt = 0.001f; // guard
  prevMs = now;

  // --- measure yaw and compute shortest signed error (-180..+180) ---
  float yaw = READ_YAW();
  float err = angleDiffDeg(targetYawDeg - yaw);

  // --- PID ---
  // Integral with simple anti-windup
  integ += err * dt;
  integ = constrain(integ, -maxI, maxI);

  float deriv = (err - prevErr) / dt;
  prevErr = err;

  float turn = Kp*err + Ki*integ + Kd*deriv; // positive => turn left
  // Map PID output to PWM correction; clamp
  turn = -constrain(turn, -maxTurn, +maxTurn);

  Serial.print(turn);
  Serial.print(" ");
  Serial.print(distanceDelta);
  Serial.print(" ");
  Serial.println(bot_state.dir);

  m_compound(bot_state.dir, frontSpeed, lateralSpeed, map_rotation_speed(turn));
}