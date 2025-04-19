#include "references.h"
#include "sensors.h"
#include "motors.h"
#include "pacbot_state.h"

const unsigned int gpio_pin_1 = 2; 
const unsigned int gpio_pin_2 = 3; 
const unsigned int gpio_pin_3 = 4;

int handshake_progress;

void setup()
{
  Serial.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(gpio_pin_1, INPUT);
  pinMode(gpio_pin_2, INPUT);
  pinMode(gpio_pin_3, INPUT);
  init_sensors();
  init_motors();

  init_state();
  handshake_progress = 3;

  digitalWrite(LED_BUILTIN, HIGH);

  Serial.println("Setup complete");

  // SYNC_BEEP(100);
  // delay(100);
  // SYNC_BEEP(100);

  randomSeed(515613);
}

long last_tick_time = 0;
void loop() {
  unsigned long now = millis();
  // imu_tick(now);

  int gpioVal = digitalRead(gpio_pin_1);
  gpioVal |= digitalRead(gpio_pin_2) << 1;
  gpioVal |= digitalRead(gpio_pin_3) << 2;

  // Handshake routine (to avoid spinning motors before raspberry pi has made contact)
  if (handshake_progress < 3) {
    if (handshake_progress == 0 && gpioVal == 3) {
      handshake_progress++;
    } else if (handshake_progress == 1) {
      if (gpioVal == 1) {
        handshake_progress++;
      } else if (gpioVal != 3) {
        handshake_progress = 0;
      }
    } else if (handshake_progress == 2) {
      if (gpioVal == 0) {
        handshake_progress++;
        calibrate();

        SYNC_BEEP(400);
        delay(100);
        SYNC_BEEP(100);
      } else if (gpioVal != 1) {
        handshake_progress = 0;
      }
    }
    STOP();
    Serial.println(handshake_progress);
    delay(100);
    return;
  }

  if (gpioVal == 0) {
    STOP();
  } else if (gpioVal <= 4) {
    START();
    SET_DIR(gpioVal - 1);
    recovery_counter = 0;
    // START();
    // recovery(now - last_tick_time);
  } else if (gpioVal == 5) {
    init_state();
    calibrate();
    SYNC_BEEP(80);
    delay(80);
  } else if (gpioVal == 6) {
    START();
    // m_clockwise(ROTATIONAL_CORRECTION_SPEED);
    recovery(now - last_tick_time);
  } 

  movement_tick(now - last_tick_time, gpioVal);
  last_tick_time = now;
}

