// =====================================================================
//  RPiPacBot -- Raspberry Pi Pico 2 W robot controller
//
//  Core 0: Sensor reading, motor control, state machine
//  Core 1: WiFi + UDP communication (see comms.cpp)
// =====================================================================

#include "config.h"
#include "sensors.h"
#include "motors.h"
#include "drive.h"
#include "comms.h"

// =====================================================================
//  Core 0 -- setup() / loop()
// =====================================================================

void setup() {
    Serial.begin(115200);

    sensorsHardwareInit();

    // Init shared data before core 1 starts WiFi
    commsSharedInit();

    delay(500);

    // Scan sensors FIRST, before WiFi or motors, so the I2C bus is
    // free from interrupt interference (WiFi runs on core 1).
    sensorsInit();
    motorsInit();
    driveInit();

    // Populate device info for core 1 to send once WiFi connects
    uint8_t mask = 0;
    for (uint8_t i = 0; i < MAX_SENSORS; i++) {
        if (sensorPresent[i]) mask |= (1 << i);
    }
    commsSetDeviceInfo(numSensorsPresent, mask, imuPresent);
}

// How often the heading PID runs (ms)
#define DRIVE_UPDATE_MS 10

void loop() {
    static uint32_t lastRead  = 0;
    static uint32_t lastDrive = 0;

    // -- Drive mode changes (handled in every state) -------------------
    DriveMode newMode;
    if (commsPollDriveMode(newMode)) {
        float yaw = 0, pitch = 0, roll = 0;
        imuReadEuler(yaw, pitch, roll);
        driveSetMode(newMode, yaw);
    }

    // -- Heading calibration -------------------------------------------
    if (commsPollCalibrate()) {
        float yaw = 0, pitch = 0, roll = 0;
        imuReadEuler(yaw, pitch, roll);
        driveCalibrateHeading(yaw);
    }

    // -- Motor / drive control -----------------------------------------
    if (driveGetMode() == DRIVE_CARDINAL_LOCKED) {
        // Accept cardinal direction commands
        CardinalDir dir;
        uint8_t speed;
        if (commsPollCardinalCmd(dir, speed)) {
            driveSetCardinal(dir, speed);
        }

        // Run heading PID at ~100 Hz
        if (millis() - lastDrive >= DRIVE_UPDATE_MS) {
            lastDrive = millis();
            float yaw = 0, pitch = 0, roll = 0;
            imuReadEuler(yaw, pitch, roll);
            driveUpdate(yaw);
        }
    } else {
        // Manual mode: apply raw motor commands from server
        MotorState motorCmd[NUM_MOTORS];
        if (commsPollMotorCmd(motorCmd)) {
            motorsSetAll(motorCmd);
        }
    }

    // -- Sensor logging (STATE_LOGGING only) ---------------------------
    if (commsGetState() == STATE_LOGGING) {
        if (millis() - lastRead >= READ_INTERVAL_MS) {
            lastRead = millis();

            int16_t readings[MAX_SENSORS];
            uint8_t idx = 0;
            for (uint8_t i = 0; i < MAX_SENSORS; i++) {
                if (!sensorPresent[i]) continue;
                readings[idx++] = sensorReadMM(i);
            }

            float yaw = 0, pitch = 0, roll = 0;
            imuReadEuler(yaw, pitch, roll);

            uint8_t mask = 0;
            for (uint8_t i = 0; i < MAX_SENSORS; i++) {
                if (sensorPresent[i]) mask |= (1 << i);
            }

            commsPostSensorData(idx, mask, readings, imuPresent, yaw, pitch, roll);
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
