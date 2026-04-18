#pragma once

// =====================================================================
//  Global configuration for RPiPacBot
// =====================================================================

// -- WiFi credentials --------------------------------------------------
#define WIFI_SSID     "MdrcPacbot"
#define WIFI_PASSWORD ""

// -- Server (the PC running test_server.py) ----------------------------
#define SERVER_IP     "192.168.8.229"
#define SERVER_PORT   9000

// -- Timing ------------------------------------------------------------
#define READ_INTERVAL_MS      100   // sensor poll rate (10 Hz default)
#define SEND_INTERVAL_MS      100   // min gap between UDP data packets
#define ALIVE_INTERVAL_MS     1000  // heartbeat before setup

// -- VL6180X ToF sensors -----------------------------------------------
#define TOF_SDA_PIN   18
#define TOF_SCL_PIN   19
#define MAX_SENSORS   8

// CE pin and assigned I2C address for each slot.
// VL6180X boots at 0x29; we reassign sequentially at startup.
const uint8_t CE_PINS[MAX_SENSORS]   = { 17,  2,   13,  0,   1,   3,   4,   5   };
const uint8_t ADDRESSES[MAX_SENSORS] = { 0x28,0x2A,0x2B,0x2C,0x2D,0x2E,0x2F,0x30 };

// -- Sensor-to-cardinal mapping (for lane centering) -------------------
// Sensor slot index facing each direction.  Adjust to match wiring.
//   Slot 0 → CE GP17    Slot 4 → CE GP1
//   Slot 1 → CE GP2     Slot 5 → CE GP3
//   Slot 2 → CE GP13    Slot 6 → CE GP4
//   Slot 3 → CE GP0     Slot 7 → CE GP5
#define SENSOR_IDX_NORTH  0
#define SENSOR_IDX_EAST   3
#define SENSOR_IDX_SOUTH  1
#define SENSOR_IDX_WEST   2

// -- BNO055 IMU --------------------------------------------------------
#define IMU_SDA_PIN   20
#define IMU_SCL_PIN   21
#define BNO055_ADDRESS 0x28

// -- Motors (4 DC motors, each with CW and CCW PWM pins) ---------------
#define NUM_MOTORS    4
constexpr int MOTOR_CW_PINS[NUM_MOTORS]  = { 22, 11,  9, 15 };
constexpr int MOTOR_CCW_PINS[NUM_MOTORS] = { 26, 27,  8, 14 };
// NOTE: Pins 11 and 27 (motor 1) share the same PWM slice+channel.
// Only one can be in GPIO_FUNC_PWM at a time; the other must use
// GPIO_FUNC_SIO. See motors.cpp for the handling.
