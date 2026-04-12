#include "drive.h"

// -- Motor mixing vectors (must match motor_config.json) ---------------
static const float MIX_FWD[NUM_MOTORS]    = {  1,  0,  0, -1 };
static const float MIX_RIGHT[NUM_MOTORS]  = {  0, -1, -1,  0 };
static const float MIX_ROT_CW[NUM_MOTORS] = { -1, -1,  1, -1 };

// -- PID gains ---------------------------------------------------------
static const float KP = 3.0f;
static const float KI = 0.05f;
static const float KD = 0.5f;
static const float INTEGRAL_LIMIT = 100.0f;

// -- State -------------------------------------------------------------
static DriveMode   mode        = DRIVE_MANUAL;
static float       refYaw      = 0.0f;
static CardinalDir curDir      = DIR_STOP;
static uint8_t     curSpeed    = 0;

static float       pidIntegral = 0.0f;
static float       pidLastErr  = 0.0f;
static uint32_t    pidLastTime = 0;

// -- Helpers -----------------------------------------------------------

static float wrapAngle(float a) {
    while (a >=  180.0f) a -= 360.0f;
    while (a < -180.0f) a += 360.0f;
    return a;
}

// -- Public API --------------------------------------------------------

void driveInit() {
    mode        = DRIVE_MANUAL;
    pidIntegral = 0.0f;
    pidLastErr  = 0.0f;
    pidLastTime = 0;
}

void driveSetMode(DriveMode m, float currentYaw) {
    mode = m;
    if (m == DRIVE_CARDINAL_LOCKED) {
        refYaw      = currentYaw;
        pidIntegral = 0.0f;
        pidLastErr  = 0.0f;
        pidLastTime = millis();
        curDir      = DIR_STOP;
        curSpeed    = 0;
    }
    if (m == DRIVE_MANUAL) {
        motorsStop();
    }
}

DriveMode driveGetMode() {
    return mode;
}

void driveCalibrateHeading(float currentYaw) {
    refYaw      = currentYaw;
    pidIntegral = 0.0f;
    pidLastErr  = 0.0f;
    pidLastTime = millis();
}

void driveSetCardinal(CardinalDir dir, uint8_t speed) {
    curDir   = dir;
    curSpeed = speed;
}

bool driveUpdate(float currentYaw) {
    if (mode != DRIVE_CARDINAL_LOCKED) return false;

    // -- PID timing ----------------------------------------------------
    uint32_t now = millis();
    float dt = (pidLastTime == 0) ? 0.01f : (now - pidLastTime) / 1000.0f;
    if (dt < 0.001f) dt = 0.001f;
    pidLastTime = now;

    // -- Heading error (positive = need to rotate CW) ------------------
    float err = wrapAngle(refYaw - currentYaw);

    pidIntegral += err * dt;
    pidIntegral  = constrain(pidIntegral, -INTEGRAL_LIMIT, INTEGRAL_LIMIT);

    float deriv = (err - pidLastErr) / dt;
    pidLastErr  = err;

    float correction = KP * err + KI * pidIntegral + KD * deriv;
    correction = constrain(correction, -255.0f, 255.0f);

    // -- Base movement vector ------------------------------------------
    float fwd = 0.0f, right = 0.0f;
    switch (curDir) {
        case DIR_NORTH: fwd   =  1.0f; break;
        case DIR_SOUTH: fwd   = -1.0f; break;
        case DIR_EAST:  right =  1.0f; break;
        case DIR_WEST:  right = -1.0f; break;
        default: break;
    }

    // -- Mix: base movement + heading correction -----------------------
    MotorState out[NUM_MOTORS];
    for (uint8_t i = 0; i < NUM_MOTORS; i++) {
        float val = fwd   * MIX_FWD[i]    * curSpeed
                  + right * MIX_RIGHT[i]   * curSpeed
                  + correction * MIX_ROT_CW[i];
        int16_t clamped = constrain((int16_t)val, -255, 255);
        out[i].direction = (clamped < 0) ? 1 : 0;
        out[i].speed     = (uint8_t)abs(clamped);
    }

    motorsSetAll(out);
    return true;
}
