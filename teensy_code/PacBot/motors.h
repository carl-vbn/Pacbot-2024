const uint8_t MOTORCW_PINS[4]  = {15, 23, 5, 8};
const uint8_t MOTORCCW_PINS[4] = {14, 22, 6, 7};

#define CW(n, speed) analogWrite(MOTORCW_PINS[n], speed)
#define CCW(n, speed) analogWrite(MOTORCCW_PINS[n], speed)

void init_motors() {
  for (int i = 0; i<4; i++) {
    pinMode(MOTORCW_PINS[i], OUTPUT);
    pinMode(MOTORCCW_PINS[i], OUTPUT);

    analogWrite(MOTORCW_PINS[i], 0);
    analogWrite(MOTORCCW_PINS[i], 0);
  }
}

void m_north(int speed, int rightBias) {
  CW(0, 0);
  CW(1, speed + rightBias);
  CW(2, 0);
  CW(3, 0);
  CCW(0, 0);
  CCW(1, 0);
  CCW(2, 0);
  CCW(3, speed - rightBias);
}

void m_south(int speed, int rightBias) {
  CW(0, 0);
  CW(1, 0);
  CW(2, 0);
  CW(3, speed + rightBias);
  CCW(0, 0);
  CCW(1, speed - rightBias);
  CCW(2, 0);
  CCW(3, 0);
}

void m_east(int speed, int rightBias) {
  CW(0, 0);
  CW(1, 0);
  CW(2, speed + rightBias);
  CW(3, 0);
  CCW(0, speed - rightBias);
  CCW(1, 0);
  CCW(2, 0);
  CCW(3, 0);
}

void m_west(int speed, int rightBias) {
  CW(0, speed + rightBias);
  CW(1, 0);
  CW(2, 0);
  CW(3, 0);
  CCW(0, 0);
  CCW(1, 0);
  CCW(2, speed - rightBias);
  CCW(3, 0);
}

void m_clockwise(int speed) {
  for (int i = 0; i<4; i++) {
    CW(i, 0);
    CCW(i, speed);
  }
}

void m_counterclockwise(int speed) {
  for (int i = 0; i<4; i++) {
    CW(i, speed);
    CCW(i, 0);
  }
}

void m_compound(uint8_t dir, int frontSpeed, int lateralSpeed, int rotation) {
  int speeds[4];

  // Calculate mixed speed for each motor
  speeds[0] =  0          - lateralSpeed + rotation;
  speeds[1] =  frontSpeed + 0            + rotation;
  speeds[2] =  0          + lateralSpeed + rotation;
  speeds[3] = -frontSpeed - 0            + rotation;

  // Apply speeds to motors
  for (int i = 0; i < 4; i++) {
    int speed = speeds[(i + 4 - dir) % 4];
    if (speed > 0) {
      CW(i, speed);
      CCW(i, 0);
    } else {
      CW(i, 0);
      CCW(i, -speed);
    }
  }
}


void m_beep() {
  for (uint8_t i = 0; i<4; i++) {
    CW(i, 40);
    CCW(i, 0);
  }
}

#define SYNC_BEEP(length) do { m_beep(); delay(length); m_stop(); } while(false);

void m_stop() {
  for (uint8_t i = 0; i<4; i++) {
    CW(i, 0);
    CCW(i, 0);
  }
}