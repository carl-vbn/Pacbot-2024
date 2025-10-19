#include <Wire.h>
#include <VL6180X.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BNO055.h>
#include <utility/imumaths.h>

VL6180X sensors[4];
const int sensor_pins[4] = {11, 12, 9, 10};
int imu_yaw;

// User macros
#define READ_DIR_DIST(dir) (sensors[(dir)].readRangeContinuous())
#define READ_YAW() (imu_yaw)

// If ADR is LOW (default) -> 0x28 ; if ADR is HIGH -> 0x29
constexpr uint8_t BNO_ADDR = 0x28;

// Use the primary I2C bus (&Wire on pins 18/19 for Teensy 4.0)
Adafruit_BNO055 bno(55, BNO_ADDR, &Wire1);

void printCalStatus() {
  uint8_t sys, gyro, accel, mag;
  bno.getCalibration(&sys, &gyro, &accel, &mag);
  Serial.print(" Calib(SYS,G,A,M)=");
  Serial.print(sys); Serial.print(",");
  Serial.print(gyro); Serial.print(",");
  Serial.print(accel); Serial.print(",");
  Serial.print(mag);
}

void init_bno() {
  if (!bno.begin()) {
    Serial.println("ERROR: No BNO055 detected. Check wiring & I2C address (ADR pin).");
    // Don't hard hang: keep printing a heartbeat so you can see it's alive
    for (;;) {
      Serial.println(" Waiting for BNO055...");
      delay(1000);
    }
  }

  // Give the sensor time to boot fully
  delay(1000);

  // Use external crystal for better accuracy if your breakout has one (Adafruit board does)
  bno.setExtCrystalUse(true);

  // Optional: set to NDOF fusion mode explicitly (default is NDOF after begin)
  // bno.setMode(Adafruit_BNO055::OPERATION_MODE_NDOF);

  Serial.println("BNO055 initialized.\n");

  imu_yaw = 0;
}

void init_sensors() {
  for (int i = 0; i<4; i++) {
    pinMode(sensor_pins[i], OUTPUT);
    digitalWrite(sensor_pins[i], LOW);
  }

  delay(100);

  Serial.println("Initializing IR sensors...");
  Wire.begin();
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
    Serial.println(sensors[i].readRangeContinuous());
  }

  // BNO055 Initialization

  // Speed up I2C a bit (Teensy 4.0 easily handles 400kHz)
  Wire.setClock(400000);

  // Try to init the BNO055
  init_bno();
}

void calibrate_imu() {
  // TODO Maybe there is something we can calibrate here?
}

void imu_tick() {
    sensors_event_t event;
    bno.getEvent(&event, Adafruit_BNO055::VECTOR_EULER);

    // Euler angles are in degrees: x=heading (yaw), y=roll, z=pitch
    imu_yaw = event.orientation.x;

    // Print calibration + self test / system status
    // printCalStatus();

    // You can also check overall system status & self test:
    // uint8_t system_status, self_test, system_error;
    // bno.getSystemStatus(&system_status, &self_test, &system_error);
    // Serial.print("  Sys=");
    // Serial.print(system_status);
    // Serial.print("  Err=");
    // Serial.print(system_error);

    // Serial.println();

}