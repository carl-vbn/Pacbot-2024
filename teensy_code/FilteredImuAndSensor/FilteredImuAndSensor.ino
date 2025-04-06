#include <Wire.h>

#define MPU_ADDR       0x68
#define GYRO_ZOUT_H    0x47
#define PWR_MGMT_1     0x6B
#define VL6180X_ADDR   0x29
#define VL6180X_RANGE_START  0x018  // Range start command register
#define VL6180X_RESULT_RANGE 0x062  // Range result register

TwoWire &MPUWire = Wire1;  // MPU6050 on secondary I2C (e.g., pins 16/17)
float yaw = 0.0;
float bias_z = 0.0;
float filtered_rate_z = 0.0;
const float alpha = 0.95;

unsigned long prevMillis = 0;
const unsigned long interval = 10;

void setup() {
  Serial.begin(115200);
  delay(1000);

  // Init I2C buses
  Wire.begin();      // VL6180X on primary I2C
  MPUWire.begin();   // MPU6050 on secondary I2C

  // Wake up MPU6050
  MPUWire.beginTransmission(MPU_ADDR);
  MPUWire.write(PWR_MGMT_1);
  MPUWire.write(0);
  MPUWire.endTransmission();

  // Calibrate MPU6050 Z gyro bias
  long sum = 0;
  for (int i = 0; i < 100; i++) {
    sum += readGyroZRaw();
    delay(10);
  }
  bias_z = sum / 100.0;
  Serial.print("Gyro Z bias: ");
  Serial.println(bias_z);

  // Initialize VL6180X
  initVL6180X();
  prevMillis = millis();
}

void loop() {
  unsigned long now = millis();
  if (now - prevMillis >= interval) {
    float dt = (now - prevMillis) / 1000.0;
    prevMillis = now;

    // MPU6050 Yaw
    float z_raw = readGyroZRaw();
    float z_rate = (z_raw - bias_z) / 131.0;
    filtered_rate_z = alpha * filtered_rate_z + (1 - alpha) * z_rate;
    yaw += filtered_rate_z * dt;
    yaw *= 0.9995;

    // VL6180X distance
    uint8_t distance = readDistanceVL6180X();

    Serial.print("Yaw: ");
    Serial.print(yaw);
    Serial.print(" deg, Distance: ");
    Serial.print(distance);
    Serial.println(" mm");
  }
}

// === MPU6050 ===
int16_t readGyroZRaw() {
  MPUWire.beginTransmission(MPU_ADDR);
  MPUWire.write(GYRO_ZOUT_H);
  MPUWire.endTransmission(false);
  MPUWire.requestFrom(MPU_ADDR, 2);

  if (MPUWire.available() < 2) return 0;
  uint8_t high = MPUWire.read();
  uint8_t low  = MPUWire.read();
  return (int16_t)((high << 8) | low);
}

// === VL6180X ===
void initVL6180X() {
  // Recommended init sequence (minimal) — could expand with full config
  writeReg16(VL6180X_ADDR, 0x0207, 0x01);
  writeReg16(VL6180X_ADDR, 0x0208, 0x01);
  writeReg16(VL6180X_ADDR, 0x0096, 0x00);
  writeReg16(VL6180X_ADDR, 0x0097, 0xfd);
  writeReg16(VL6180X_ADDR, 0x00e3, 0x00);
  writeReg16(VL6180X_ADDR, 0x00e4, 0x04);
  writeReg16(VL6180X_ADDR, 0x00e5, 0x02);
  writeReg16(VL6180X_ADDR, 0x00e6, 0x01);
  writeReg16(VL6180X_ADDR, 0x00e7, 0x03);
  writeReg16(VL6180X_ADDR, 0x00f5, 0x02);
  writeReg16(VL6180X_ADDR, 0x00d9, 0x05);
  writeReg16(VL6180X_ADDR, 0x00db, 0xce);
  writeReg16(VL6180X_ADDR, 0x00dc, 0x03);
  writeReg16(VL6180X_ADDR, 0x00dd, 0xf8);
  writeReg16(VL6180X_ADDR, 0x009f, 0x00);
  writeReg16(VL6180X_ADDR, 0x00a3, 0x3c);
  writeReg16(VL6180X_ADDR, 0x00b7, 0x00);
  writeReg16(VL6180X_ADDR, 0x00bb, 0x3c);
  writeReg16(VL6180X_ADDR, 0x00b2, 0x09);
  writeReg16(VL6180X_ADDR, 0x00ca, 0x09);
  writeReg16(VL6180X_ADDR, 0x0198, 0x01);
  writeReg16(VL6180X_ADDR, 0x01b0, 0x17);
  writeReg16(VL6180X_ADDR, 0x01ad, 0x00);
  writeReg16(VL6180X_ADDR, 0x00ff, 0x05);
  writeReg16(VL6180X_ADDR, 0x0100, 0x05);
  writeReg16(VL6180X_ADDR, 0x0199, 0x05);
  writeReg16(VL6180X_ADDR, 0x01a6, 0x1b);
  writeReg16(VL6180X_ADDR, 0x01ac, 0x3e);
  writeReg16(VL6180X_ADDR, 0x01a7, 0x1f);
  writeReg16(VL6180X_ADDR, 0x0030, 0x00);
}

uint8_t readDistanceVL6180X() {
  writeReg16(VL6180X_ADDR, VL6180X_RANGE_START, 0x01);  // Start ranging
  delay(10);  // Short delay for measurement to complete

  // Read result
  Wire.beginTransmission(VL6180X_ADDR);
  Wire.write((uint8_t)(VL6180X_RESULT_RANGE >> 8));  // MSB of reg addr
  Wire.write((uint8_t)(VL6180X_RESULT_RANGE & 0xFF));  // LSB of reg addr
  Wire.endTransmission(false);
  Wire.requestFrom(VL6180X_ADDR, 1);
  if (Wire.available()) {
    return Wire.read();  // Distance in mm
  } else {
    return 0;  // Error
  }
}

void writeReg16(uint8_t addr, uint16_t reg, uint8_t value) {
  Wire.beginTransmission(addr);
  Wire.write((uint8_t)(reg >> 8));      // MSB
  Wire.write((uint8_t)(reg & 0xFF));    // LSB
  Wire.write(value);
  Wire.endTransmission();
}
