#include <Wire.h>
#include <VL6180X.h>

VL6180X sensors[4];
const int sensor_pins[4] = {11, 12, 9, 10};

// MPU6050 Config
#define MPU_ADDR       0x68
#define GYRO_ZOUT_H    0x47
#define PWR_MGMT_1     0x6B

// User macros
#define READ_DIR_DIST(dir) (sensors[(dir)].readRangeContinuous())
#define READ_YAW() (imu_yaw)

TwoWire &MPUWire = Wire1;  // MPU6050 on secondary I2C (e.g., pins 16/17)
float imu_yaw = 0.0;
float imu_bias_z = 0.0;
float imu_filtered_rate_z = 0.0;
const float imu_alpha = 0.98; // used to be 0.95

unsigned long imu_prevMillis = 0;
const unsigned long imu_interval = 5; // used to be 10

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

void calibrate_imu() {
  // Calibrate MPU6050 Z gyro bias
  long sum = 0;
  for (int i = 0; i < 100; i++) {
    sum += _readGyroZRaw();
    delay(10);
  }
  imu_bias_z = sum / 100.0;

  imu_yaw = 0.0;
  Serial.println("Calibrated IMU");
}

void init_sensors() {
  for (int i = 0; i<4; i++) {
    pinMode(sensor_pins[i], OUTPUT);
    digitalWrite(sensor_pins[i], LOW);
  }

  Wire.begin();
  MPUWire.begin();   // MPU6050 on secondary I2C

  // Wake up MPU6050
  Serial.println("Initializing IMU...");
  MPUWire.beginTransmission(MPU_ADDR);
  MPUWire.write(PWR_MGMT_1);
  MPUWire.write(0);
  MPUWire.endTransmission();

  calibrate_imu();

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

  imu_prevMillis = millis();
}

void imu_tick(unsigned long now) {
  if (now - imu_prevMillis >= imu_interval) {
    float dt = (now - imu_prevMillis) / 1000.0;
    imu_prevMillis = now;

    // MPU6050 Yaw
    float z_raw = _readGyroZRaw();
    float z_rate = (z_raw - imu_bias_z) / 131.0;
    imu_filtered_rate_z = imu_alpha * imu_filtered_rate_z + (1 - imu_alpha) * z_rate;
    imu_yaw += imu_filtered_rate_z * dt;
    imu_yaw *= 0.999;
  }
}