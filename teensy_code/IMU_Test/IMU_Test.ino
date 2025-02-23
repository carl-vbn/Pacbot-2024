#include "Wire.h"
#include <MPU6050_light.h>

MPU6050 mpu(Wire);
unsigned long timer = 0;
float yaw = 0; // Yaw angle (rotation around Z)
float alpha = 0.98; // Complementary filter weight

void setup() {
    Serial.begin(115200);
    Wire.begin();
  
    byte status = mpu.begin();
    Serial.print(F("MPU6050 status: "));
    Serial.println(status);
    while (status != 0) {} // Stop if MPU6050 is not connected
  
    Serial.println(F("Calculating offsets, do not move MPU6050"));
    delay(1000);
    mpu.calcOffsets(); // Calibrate gyro and accelerometer
    Serial.println("Done!\n");
}

void loop() {
    mpu.update();

    if ((millis() - timer) > 10) {  // Every 10ms
        float gyroZ = mpu.getGyroZ(); // Get raw gyro Z-axis value
        float dt = (millis() - timer) / 1000.0; // Convert to seconds

        // Complementary filter to reduce yaw drift
        yaw = alpha * (yaw + gyroZ * dt) + (1 - alpha) * mpu.getAngleZ();
      
        Serial.print("X : ");
        Serial.print(mpu.getAngleX());
        Serial.print("  Y : ");
        Serial.print(mpu.getAngleY());
        Serial.print("  Yaw (Filtered) : ");
        Serial.println(yaw);
      
        timer = millis();
    }
}

