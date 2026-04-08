#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <VL6180X.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BNO055.h>
#include "config.h"

// Sensor state -- populated by sensorsInit()
extern bool    sensorPresent[MAX_SENSORS];
extern uint8_t numSensorsPresent;
extern bool    imuPresent;

// Initialise I2C buses and hold all CE pins low.
// Call once from setup() before any sensor work.
void sensorsHardwareInit();

// Sequentially bring up all VL6180X sensors and the BNO055.
// Returns the number of ToF sensors found.
uint8_t sensorsInit();

// Read distance from one ToF sensor (mm). Returns -1 on error/absent.
int16_t sensorReadMM(uint8_t index);

// Read BNO055 Euler angles into yaw/pitch/roll (degrees).
// Returns false if IMU is absent.
bool imuReadEuler(float &yaw, float &pitch, float &roll);
