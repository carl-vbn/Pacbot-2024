#pragma once

#include <Arduino.h>
#include <pico/mutex.h>
#include "config.h"
#include "motors.h"
#include "drive.h"
#include "protocol.h"

// =====================================================================
//  Dual-core communication layer
//
//  Core 0 (main loop) writes sensor data and reads motor commands.
//  Core 1 handles WiFi, UDP send/receive, and protocol logic.
//  All shared state is protected by a mutex.
// =====================================================================

// -- Robot state machine (shared) -------------------------------------
enum RobotState : uint8_t {
    STATE_IDLE,          // sending alive heartbeats, waiting for CMD_START_LOG
    STATE_LOGGING,       // continuous data streaming
};

// -- Shared data structure (protected by commsSharedMutex) -------------
struct SharedData {
    // Written by core 0, read by core 1
    int16_t  sensorReadings[MAX_SENSORS];
    uint8_t  sensorCount;           // number of present sensors
    uint8_t  sensorMask;            // bitmask of present slots
    bool     hasImu;
    float    yaw, pitch, roll;
    uint32_t dataTimestamp;         // millis() when readings were taken

    // Written by core 1, read by core 0
    MotorState motorCmd[NUM_MOTORS];
    bool       motorCmdPending;     // set true by core 1, cleared by core 0

    uint16_t sendIntervalMs;        // configurable data send rate

    // Drive mode command (written by core 1, read by core 0)
    DriveMode  pendingDriveMode;
    bool       driveModeCmdPending;

    // Cardinal direction command (written by core 1, read by core 0)
    CardinalDir pendingCardinalDir;
    uint8_t     pendingCardinalSpeed;
    bool        cardinalCmdPending;

    bool        calibratePending;

    // Set by core 0 after commsSetDeviceInfo(); core 1 waits for this
    // before sending DEVICE_INFO to the server.
    bool     deviceInfoReady;

    // State machine -- transitions negotiated between cores
    volatile RobotState state;
};

extern SharedData  commsShared;
extern mutex_t     commsSharedMutex;

// -- Core 0 helpers (call from main loop) -----------------------------

// Set device info (sensor mask, count, IMU presence) once at startup.
void commsSetDeviceInfo(uint8_t count, uint8_t mask, bool hasImu);

// Post fresh sensor readings into shared data.
void commsPostSensorData(uint8_t count, uint8_t mask, const int16_t *readings,
                         bool hasImu, float yaw, float pitch, float roll);

// Check if a motor command arrived. If so, copies into `out` and returns true.
bool commsPollMotorCmd(MotorState out[NUM_MOTORS]);

// Check if a drive mode change was requested. Returns true once, then clears.
bool commsPollDriveMode(DriveMode &mode);

// Check if a cardinal direction was set. Returns true once, then clears.
bool commsPollCardinalCmd(CardinalDir &dir, uint8_t &speed);

// Check if a calibrate was requested. Returns true once, then clears.
bool commsPollCalibrate();

// Read current robot state (lock-free volatile read).
RobotState commsGetState();

// Transition state (under mutex).
void commsSetState(RobotState s);

// -- Shared data init (call from core 0 setup, before commsSetDeviceInfo) --
void commsSharedInit();

// -- Core 1 entry points (called by setup1 / loop1) -------------------
void commsSetup();   // connect WiFi, open UDP socket
void commsLoop();    // handle send/receive cycle
