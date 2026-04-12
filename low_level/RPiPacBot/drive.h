#pragma once

#include <Arduino.h>
#include "config.h"
#include "motors.h"

// -- Drive modes -------------------------------------------------------
enum DriveMode : uint8_t {
    DRIVE_MANUAL         = 0,   // server sends raw motor speeds
    DRIVE_CARDINAL_LOCKED = 1,  // server sends cardinal directions,
                                // PID maintains heading
};

// -- Cardinal directions -----------------------------------------------
enum CardinalDir : uint8_t {
    DIR_STOP  = 0,
    DIR_NORTH = 1,
    DIR_EAST  = 2,
    DIR_SOUTH = 3,
    DIR_WEST  = 4,
};

// Initialise drive subsystem (call once from setup).
void driveInit();

// Switch drive mode.  When entering CARDINAL_LOCKED, currentYaw is
// captured as the reference heading.  Switching to MANUAL stops motors.
void driveSetMode(DriveMode mode, float currentYaw);

// Get the current drive mode.
DriveMode driveGetMode();

// Calibrate: record reference heading and lateral distances for
// single-wall centering fallback.
void driveCalibrate(float currentYaw, int16_t leftDist, int16_t rightDist);

// Set the active cardinal direction and base speed (0-255).
void driveSetCardinal(CardinalDir dir, uint8_t speed);

// Run one heading-PID iteration with the latest IMU yaw.
// Also applies the most recent lateral correction.
// Returns true if motors were updated.
bool driveUpdate(float currentYaw);

// Run one centering-PID iteration with fresh lateral sensor readings.
// leftDist / rightDist come from the sensors returned by
// driveGetLateralSensors().  Pass -1 if a sensor is absent/errored.
void driveUpdateCentering(int16_t leftDist, int16_t rightDist);

// Update forward distance PID.  Maintains FORWARD_TARGET_MM from the
// wall ahead — backs off when too close, advances when too far.
void driveUpdateForward(int16_t forwardDist);

// Get the sensor index ahead of the current movement direction.
// Returns false when stopped.
bool driveGetForwardSensor(uint8_t &idx);

// Get the sensor indices for the perpendicular axis of the current
// cardinal direction.  Returns false when stopped / no centering needed.
bool driveGetLateralSensors(uint8_t &leftIdx, uint8_t &rightIdx);
