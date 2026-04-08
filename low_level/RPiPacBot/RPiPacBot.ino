// =====================================================================
//  RPiPacBot -- Raspberry Pi Pico 2 W robot controller
//
//  Core 0: Sensor reading, motor control, state machine
//  Core 1: WiFi + UDP communication (see comms.cpp)
// =====================================================================

#include "config.h"
#include "sensors.h"
#include "motors.h"
#include "comms.h"

// =====================================================================
//  Core 0 -- setup() / loop()
// =====================================================================

void setup() {
    Serial.begin(115200);

    // Hardware init (CE pins low, I2C buses up) while WiFi connects on core 1
    sensorsHardwareInit();
    motorsInit();
}

void loop() {
    static uint32_t lastRead = 0;

    RobotState st = commsGetState();

    // -- State machine (core 0 side) -----------------------------------
    switch (st) {
        case STATE_IDLE:
            // Nothing to do -- core 1 sends alive heartbeats
            break;

        case STATE_SETUP_REQ: {
            // Server asked us to init sensors
            sensorsInit();

            // Build sensor presence bitmask
            uint8_t mask = 0;
            for (uint8_t i = 0; i < MAX_SENSORS; i++) {
                if (sensorPresent[i]) mask |= (1 << i);
            }

            // Post device info so core 1 can send it
            commsPostSensorData(numSensorsPresent, mask, nullptr,
                                imuPresent, 0, 0, 0);

            commsSetState(STATE_SETUP_DONE);
            break;
        }

        case STATE_SETUP_DONE: {
            // Waiting for CMD_START_LOG -- but motor commands are accepted
            MotorState motorCmd[NUM_MOTORS];
            if (commsPollMotorCmd(motorCmd)) {
                motorsSetAll(motorCmd);
            }
            break;
        }

        case STATE_LOGGING: {
            if (millis() - lastRead < READ_INTERVAL_MS) break;
            lastRead = millis();

            // Read all present ToF sensors
            int16_t readings[MAX_SENSORS];
            uint8_t idx = 0;
            for (uint8_t i = 0; i < MAX_SENSORS; i++) {
                if (!sensorPresent[i]) continue;
                readings[idx++] = sensorReadMM(i);
            }

            // Read IMU
            float yaw = 0, pitch = 0, roll = 0;
            bool hasImu = imuReadEuler(yaw, pitch, roll);

            // Build mask
            uint8_t mask = 0;
            for (uint8_t i = 0; i < MAX_SENSORS; i++) {
                if (sensorPresent[i]) mask |= (1 << i);
            }

            // Post to shared buffer for core 1
            commsPostSensorData(idx, mask, readings, hasImu, yaw, pitch, roll);

            // Check for motor commands from server
            MotorState motorCmd[NUM_MOTORS];
            if (commsPollMotorCmd(motorCmd)) {
                motorsSetAll(motorCmd);
            }

            break;
        }
    }
}

// =====================================================================
//  Core 1 -- setup1() / loop1()
//  The arduino-pico core automatically runs these on the second core.
// =====================================================================

void setup1() {
    commsSetup();
}

void loop1() {
    commsLoop();
}
