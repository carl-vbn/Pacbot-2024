#pragma once

#include <Arduino.h>
#include "config.h"

struct MotorState {
    uint8_t direction;  // 0 = CW, 1 = CCW
    uint8_t speed;      // 0-255 PWM duty
};

// Initialise motor GPIO pins. Call once from setup().
void motorsInit();

// Set a single motor. Index 0..NUM_MOTORS-1.
void motorSet(uint8_t index, uint8_t direction, uint8_t speed);

// Apply an array of NUM_MOTORS motor states at once.
void motorsSetAll(const MotorState states[NUM_MOTORS]);

// Emergency stop -- all motors to speed 0.
void motorsStop();
