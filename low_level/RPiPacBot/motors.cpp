#include "motors.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"

// Motor 1 (pins 11 CW, 27 CCW) shares a PWM slice+channel.
// We track which pin currently owns the PWM function so we only
// reconfigure on direction changes.
#define CONFLICTING_MOTOR 1
static int8_t motor1_pwm_pin = -1;  // which pin is currently GPIO_FUNC_PWM

// For the conflicting motor: put `active` into PWM mode and drive it at
// `duty`, put `idle` into SIO mode and hold it LOW.
static void configConflicting(int active, int idle, uint8_t duty) {
    if (motor1_pwm_pin != active) {
        // Switch functions
        gpio_set_function(idle, GPIO_FUNC_SIO);
        gpio_set_dir(idle, GPIO_OUT);
        gpio_put(idle, 0);

        gpio_set_function(active, GPIO_FUNC_PWM);
        motor1_pwm_pin = active;
    } else {
        gpio_put(idle, 0);
    }
    analogWrite(active, duty);
}

void motorsInit() {
    for (uint8_t i = 0; i < NUM_MOTORS; i++) {
        if (i == CONFLICTING_MOTOR) {
            // Start with CW pin as PWM, CCW pin as SIO held LOW
            gpio_set_function(MOTOR_CW_PINS[i], GPIO_FUNC_PWM);
            analogWrite(MOTOR_CW_PINS[i], 0);

            gpio_set_function(MOTOR_CCW_PINS[i], GPIO_FUNC_SIO);
            gpio_set_dir(MOTOR_CCW_PINS[i], GPIO_OUT);
            gpio_put(MOTOR_CCW_PINS[i], 0);

            motor1_pwm_pin = MOTOR_CW_PINS[i];
        } else {
            pinMode(MOTOR_CW_PINS[i], OUTPUT);
            pinMode(MOTOR_CCW_PINS[i], OUTPUT);
            analogWrite(MOTOR_CW_PINS[i], 0);
            analogWrite(MOTOR_CCW_PINS[i], 0);
        }
    }
}

void motorSet(uint8_t index, uint8_t direction, uint8_t speed) {
    if (index >= NUM_MOTORS) return;

    int cw  = MOTOR_CW_PINS[index];
    int ccw = MOTOR_CCW_PINS[index];

    if (index == CONFLICTING_MOTOR) {
        if (speed == 0) {
            // Both off
            analogWrite(motor1_pwm_pin, 0);
            gpio_put(motor1_pwm_pin == cw ? ccw : cw, 0);
        } else if (direction == 0) {
            configConflicting(cw, ccw, speed);
        } else {
            configConflicting(ccw, cw, speed);
        }
    } else {
        if (direction == 0) {
            analogWrite(cw, speed);
            analogWrite(ccw, 0);
        } else {
            analogWrite(cw, 0);
            analogWrite(ccw, speed);
        }
    }
}

void motorsSetAll(const MotorState states[NUM_MOTORS]) {
    for (uint8_t i = 0; i < NUM_MOTORS; i++) {
        motorSet(i, states[i].direction, states[i].speed);
    }
}

void motorsStop() {
    for (uint8_t i = 0; i < NUM_MOTORS; i++) {
        motorSet(i, 0, 0);
    }
}
