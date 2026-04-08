#pragma once

#include <Arduino.h>
#include <pico/mutex.h>
#include "config.h"
#include "motors.h"
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
    STATE_IDLE,          // sending alive heartbeats, waiting for CMD_SETUP
    STATE_SETUP_REQ,     // core 0 should run sensor init
    STATE_SETUP_DONE,    // sensor init complete, info ready to send
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

    // State machine -- transitions negotiated between cores
    volatile RobotState state;
};

extern SharedData  commsShared;
extern mutex_t     commsSharedMutex;

// -- Core 0 helpers (call from main loop) -----------------------------

// Post fresh sensor readings into shared data.
void commsPostSensorData(uint8_t count, uint8_t mask, const int16_t *readings,
                         bool hasImu, float yaw, float pitch, float roll);

// Check if a motor command arrived. If so, copies into `out` and returns true.
bool commsPollMotorCmd(MotorState out[NUM_MOTORS]);

// Read current robot state (lock-free volatile read).
RobotState commsGetState();

// Transition state (under mutex).
void commsSetState(RobotState s);

// -- Core 1 entry points (called by setup1 / loop1) -------------------
void commsSetup();   // connect WiFi, open UDP socket
void commsLoop();    // handle send/receive cycle
