const uint8_t MOTORCW_PINS[4]  = {14, 7, 22, 6};
const uint8_t MOTORCCW_PINS[4] = {15, 8, 23, 5};

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

void m_stop() {
  for (uint8_t i = 0; i<4; i++) {
    CW(i, 0);
    CCW(i, 0);
  }
}