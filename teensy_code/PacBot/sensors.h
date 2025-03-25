#include <Wire.h>
#include <VL6180X.h>
#include <MPU6050_light.h>

VL6180X sensors[4];
MPU6050 mpu(Wire);

const int sensor_pins[4] = {11, 12, 9, 10};

void init_sensors() {
  for (int i = 0; i<4; i++) {
    pinMode(sensor_pins[i], OUTPUT);
    digitalWrite(sensor_pins[i], LOW);
  }

  Wire.begin();

  delay(500);

  // byte status = mpu.begin();
  // Serial.print(F("MPU6050 status: "));
  // Serial.println(status);
  // while (status != 0) {
  //   Serial.print("MPU ERROR ");
  //   Serial.println(status);
  //   delay(200);
  // }

  for (int i = 0; i<4; i++) {
    digitalWrite(sensor_pins[i], HIGH);
    delay(50);
    sensors[i].init();
    sensors[i].configureDefault();
    sensors[i].setTimeout(500);
    sensors[i].setAddress(0x54 + i * 2);
  }

  delay(100);

  for (int i = 0; i<4; i++) {
    sensors[i].startRangeContinuous();
  }
}