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

// Reset the reference heading to the given yaw.
void driveCalibrateHeading(float currentYaw);

// Set the active cardinal direction and base speed (0-255).
void driveSetCardinal(CardinalDir dir, uint8_t speed);

// Run one PID iteration with the latest IMU yaw.
// Computes motor outputs and applies them.  Only acts in CARDINAL_LOCKED
// mode.  Returns true if motors were updated.
bool driveUpdate(float currentYaw);
