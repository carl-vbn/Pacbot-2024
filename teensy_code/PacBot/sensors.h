#include <Wire.h>
#include <VL6180X.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BNO055.h>
#include <utility/imumaths.h>

VL6180X sensors[4];
const int sensor_pins[4] = {11, 12, 9, 10};

// MPU6050 Config
#define MPU_ADDR       0x68
#define GYRO_ZOUT_H    0x47
#define PWR_MGMT_1     0x6B

// User macros
#define READ_DIR_DIST(dir) (sensors[(dir)].readRangeContinuous())
//#define READ_YAW() (imu_yaw)

TwoWire &MPUWire = Wire1;  // MPU6050 on secondary I2C (e.g., pins 16/17)
//float imu_yaw = 0.0;
//float imu_bias_z = 0.0;
//float imu_filtered_rate_z = 0.0;
//const float imu_alpha = 0.98; // used to be 0.95

//unsigned long imu_prevMillis = 0;
//const unsigned long imu_interval = 5; // used to be 10

// If ADR is LOW (default) -> 0x28 ; if ADR is HIGH -> 0x29
constexpr uint8_t BNO_ADDR = 0x28;

// Use the primary I2C bus (&Wire on pins 18/19 for Teensy 4.0)
Adafruit_BNO055 bno(55, BNO_ADDR, &Wire1);

int16_t _readGyroZRaw() {
  MPUWire.beginTransmission(MPU_ADDR);
  MPUWire.write(GYRO_ZOUT_H);
  MPUWire.endTransmission(false);
  MPUWire.requestFrom(MPU_ADDR, 2);

  if (MPUWire.available() < 2) return 0;
  uint8_t high = MPUWire.read();
  uint8_t low  = MPUWire.read();
  return (int16_t)((high << 8) | low);
}

// void calibrate_imu() {
//   // Calibrate MPU6050 Z gyro bias
//   long sum = 0;
//   for (int i = 0; i < 100; i++) {
//     sum += _readGyroZRaw();
//     delay(10);
//   }
//   imu_bias_z = sum / 100.0;

//   imu_yaw = 0.0;
//   Serial.println("Calibrated IMU");
// }

void printCalStatus() {
  uint8_t sys, gyro, accel, mag;
  bno.getCalibration(&sys, &gyro, &accel, &mag);
  Serial.print(" Calib(SYS,G,A,M)=");
  Serial.print(sys); Serial.print(",");
  Serial.print(gyro); Serial.print(",");
  Serial.print(accel); Serial.print(",");
  Serial.print(mag);
}

void init_sensors() {
  for (int i = 0; i<4; i++) {
    pinMode(sensor_pins[i], OUTPUT);
    digitalWrite(sensor_pins[i], LOW);

  }

  // Wire.begin();
  // MPUWire.begin();   // MPU6050 on secondary I2C

  // Wake up MPU6050
  // Serial.println("Initializing IMU...");
  // MPUWire.beginTransmission(MPU_ADDR);
  // MPUWire.write(PWR_MGMT_1);
  // MPUWire.write(0);
  // MPUWire.endTransmission();

  //calibrate_imu();

  delay(100);

  // byte status = mpu.begin();
  // Serial.print(F("MPU6050 status: "));
  // Serial.println(status);
  // while (status != 0) {
  //   Serial.print("MPU ERROR ");
  //   Serial.println(status);
  //   delay(200);
  // }

  Serial.println("Initializing IR sensors...");
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

  //imu_prevMillis = millis();

  // Faster serial for Teensy
  Serial.begin(115200);
  while (!Serial && millis() < 3000) { /* wait up to ~3s for terminal */ }

  Serial.println("\nBNO055 Orientation Test (Teensy 4.0)");

  // Speed up I2C a bit (Teensy 4.0 easily handles 400kHz)
  Wire.begin();
  Wire.setClock(400000);

  // Try to init the BNO055
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
  Serial.println("Columns: Heading(deg)\tRoll(deg)\tPitch(deg)\t| Status");

}

void imu_tick(unsigned long now) {
  // if (now - imu_prevMillis >= imu_interval) {
  //   float dt = (now - imu_prevMillis) / 1000.0;
  //   imu_prevMillis = now;

  //   // MPU6050 Yaw
  //   float z_raw = _readGyroZRaw();
  //   float z_rate = (z_raw - imu_bias_z) / 131.0;
  //   imu_filtered_rate_z = imu_alpha * imu_filtered_rate_z + (1 - imu_alpha) * z_rate;
  //   imu_yaw += imu_filtered_rate_z * dt;
  //   imu_yaw *= 0.999;
  // }

    sensors_event_t event;
    bno.getEvent(&event, Adafruit_BNO055::VECTOR_EULER);

    // Euler angles are in degrees: x=heading (yaw), y=roll, z=pitch
    Serial.print(event.orientation.x, 4); Serial.print("\t");
    Serial.print(event.orientation.y, 4); Serial.print("\t");
    Serial.print(event.orientation.z, 4); Serial.print("\t|");

    // Print calibration + self test / system status
    printCalStatus();

    // You can also check overall system status & self test:
    uint8_t system_status, self_test, system_error;
    bno.getSystemStatus(&system_status, &self_test, &system_error);
    Serial.print("  Sys=");
    Serial.print(system_status);
    Serial.print("  Err=");
    Serial.print(system_error);

    Serial.println();

    delay(100); // 10 Hz

}