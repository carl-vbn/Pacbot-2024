#include "drive.h"
#include <string.h>

// -- Motor mixing vectors (must match motor_config.json) ---------------
static const float MIX_FWD[NUM_MOTORS]    = {  1,  0,  0, -1 };
static const float MIX_RIGHT[NUM_MOTORS]  = {  0, -1, -1,  0 };
static const float MIX_ROT_CW[NUM_MOTORS] = { -1, -1,  1, -1 };

// -- Heading PID gains -------------------------------------------------
static const float KP = 2.0f;
static const float KI = 0.05f;
static const float KD = 0.1f;
static const float INTEGRAL_LIMIT = 100.0f;

// -- Centering PID gains -----------------------------------------------
static const float CENTER_KP = 1.0f;
static const float CENTER_KI = 0.0f;
static const float CENTER_KD = 0.2f;
static const float CENTER_INTEGRAL_LIMIT = 80.0f;

// -- Forward distance PID ----------------------------------------------
static const int16_t FORWARD_TARGET_MM       = 10;   // desired wall distance
static const float   FWD_KP                  = 1.0f;
static const float   FWD_KI                  = -0.2f;
static const float   FWD_KD                  = 0.01f;
static const float   FWD_INTEGRAL_LIMIT      = 80.0f;
static const float   FWD_DERIV_ALPHA         = 0.6f;  // derivative low-pass (0-1, lower = smoother)

// -- State -------------------------------------------------------------
static DriveMode   mode        = DRIVE_MANUAL;
static float       refYaw      = 0.0f;
static CardinalDir curDir      = DIR_STOP;
static uint8_t     curSpeed    = 0;

// Heading PID
static float       pidIntegral  = 0.0f;
static float       pidLastYaw   = 0.0f;  // derivative on measurement, not error
static uint32_t    pidLastTime  = 0;

// Centering
static bool        calibrated        = false;
static int16_t     refLeft           = -1;   // calibrated left-wall distance
static int16_t     refRight          = -1;   // calibrated right-wall distance
static float       centerIntegral    = 0.0f;
static float       centerLastErr     = 0.0f;
static uint32_t    centerLastTime    = 0;
static float       lateralCorrection = 0.0f;

// Forward distance PID
static float       fwdCorrection     = 0.0f;
static float       fwdIntegral       = 0.0f;
static float       fwdLastErr        = 0.0f;
static float       fwdDerivFiltered  = 0.0f;
static uint32_t    fwdLastTime       = 0;

// -- Helpers -----------------------------------------------------------

static float wrapAngle(float a) {
    while (a >=  180.0f) a -= 360.0f;
    while (a < -180.0f) a += 360.0f;
    return a;
}

// Lateral-mix direction: positive output → move "right" relative to
// the current movement direction.
static void getLateralMix(float out[NUM_MOTORS]) {
    switch (curDir) {
        case DIR_NORTH:
            for (uint8_t i = 0; i < NUM_MOTORS; i++) out[i] =  MIX_RIGHT[i];
            return;
        case DIR_SOUTH:
            for (uint8_t i = 0; i < NUM_MOTORS; i++) out[i] = -MIX_RIGHT[i];
            return;
        case DIR_EAST:
            for (uint8_t i = 0; i < NUM_MOTORS; i++) out[i] = -MIX_FWD[i];
            return;
        case DIR_WEST:
            for (uint8_t i = 0; i < NUM_MOTORS; i++) out[i] =  MIX_FWD[i];
            return;
        default:
            for (uint8_t i = 0; i < NUM_MOTORS; i++) out[i] = 0;
            return;
    }
}

// Which cardinal-direction index (0-3) the left/right sensors correspond
// to for the current movement direction.
static bool getLateralCardinals(uint8_t &leftCard, uint8_t &rightCard) {
    switch (curDir) {
        case DIR_NORTH: leftCard = 3; rightCard = 1; return true; // W, E
        case DIR_SOUTH: leftCard = 1; rightCard = 3; return true; // E, W
        case DIR_EAST:  leftCard = 0; rightCard = 2; return true; // N, S
        case DIR_WEST:  leftCard = 2; rightCard = 0; return true; // S, N
        default: return false;
    }
}

// -- Public API --------------------------------------------------------

void driveInit() {
    mode             = DRIVE_MANUAL;
    calibrated       = false;
    pidIntegral      = 0.0f;
    pidLastYaw       = 0.0f;
    pidLastTime      = 0;
    lateralCorrection = 0.0f;
}

void driveSetMode(DriveMode m, float currentYaw) {
    mode = m;
    if (m == DRIVE_CARDINAL_LOCKED) {
        refYaw      = currentYaw;
        pidIntegral = 0.0f;
        pidLastYaw  = currentYaw;
        pidLastTime = millis();
        curDir      = DIR_STOP;
        curSpeed    = 0;
        lateralCorrection = 0.0f;
        fwdCorrection     = 0.0f;
    }
    if (m == DRIVE_MANUAL) {
        motorsStop();
    }
}

DriveMode driveGetMode() {
    return mode;
}

void driveCalibrate(float currentYaw, int16_t leftDist, int16_t rightDist) {
    refYaw   = currentYaw;
    refLeft  = leftDist;
    refRight = rightDist;
    calibrated = true;

    // Reset both PIDs
    pidIntegral       = 0.0f;
    pidLastYaw        = 0.0f;
    pidLastTime       = millis();
    centerIntegral    = 0.0f;
    centerLastErr     = 0.0f;
    centerLastTime    = 0;
    lateralCorrection = 0.0f;
    fwdCorrection     = 0.0f;
    fwdIntegral       = 0.0f;
    fwdLastErr        = 0.0f;
    fwdDerivFiltered  = 0.0f;
    fwdLastTime       = 0;
}

void driveSetCardinal(CardinalDir dir, uint8_t speed) {
    if (dir != curDir) {
        // Axes changed — reset centering and forward PIDs
        centerIntegral    = 0.0f;
        centerLastErr     = 0.0f;
        centerLastTime    = 0;
        lateralCorrection = 0.0f;
        fwdCorrection     = 0.0f;
        fwdIntegral       = 0.0f;
        fwdLastErr        = 0.0f;
        fwdDerivFiltered  = 0.0f;
        fwdLastTime       = 0;
    }
    curDir   = dir;
    curSpeed = speed;
}

bool driveGetForwardSensor(uint8_t &idx) {
    static const uint8_t CARD_TO_SENSOR[4] = {
        SENSOR_IDX_NORTH, SENSOR_IDX_EAST,
        SENSOR_IDX_SOUTH, SENSOR_IDX_WEST
    };

    switch (curDir) {
        case DIR_NORTH: idx = CARD_TO_SENSOR[0]; return true;
        case DIR_EAST:  idx = CARD_TO_SENSOR[1]; return true;
        case DIR_SOUTH: idx = CARD_TO_SENSOR[2]; return true;
        case DIR_WEST:  idx = CARD_TO_SENSOR[3]; return true;
        default: return false;
    }
}

bool driveGetLateralSensors(uint8_t &leftIdx, uint8_t &rightIdx) {
    static const uint8_t CARD_TO_SENSOR[4] = {
        SENSOR_IDX_NORTH, SENSOR_IDX_EAST,
        SENSOR_IDX_SOUTH, SENSOR_IDX_WEST
    };

    uint8_t lc, rc;
    if (!getLateralCardinals(lc, rc)) return false;
    leftIdx  = CARD_TO_SENSOR[lc];
    rightIdx = CARD_TO_SENSOR[rc];
    return true;
}

void driveUpdateForward(int16_t forwardDist) {
    if (forwardDist < 0) return;           // sensor error — keep last value

    // Positive error → too far from wall → move forward
    // Negative error → too close          → back off
    float error = (float)(forwardDist - FORWARD_TARGET_MM);

    uint32_t now = millis();
    float dt = (fwdLastTime == 0) ? 0.05f : (now - fwdLastTime) / 1000.0f;
    if (dt < 0.001f) dt = 0.001f;
    fwdLastTime = now;

    fwdIntegral += error * dt;
    fwdIntegral  = constrain(fwdIntegral, -FWD_INTEGRAL_LIMIT, FWD_INTEGRAL_LIMIT);

    float rawDeriv = (error - fwdLastErr) / dt;
    fwdLastErr     = error;

    // Low-pass filter on derivative to suppress sensor noise
    fwdDerivFiltered += FWD_DERIV_ALPHA * (rawDeriv - fwdDerivFiltered);

    fwdCorrection = FWD_KP * error + FWD_KI * fwdIntegral + FWD_KD * fwdDerivFiltered;
}

bool driveUpdate(float currentYaw) {
    if (mode != DRIVE_CARDINAL_LOCKED) return false;

    // -- Heading PID ---------------------------------------------------
    uint32_t now = millis();
    float dt = (pidLastTime == 0) ? 0.01f : (now - pidLastTime) / 1000.0f;
    if (dt < 0.001f) dt = 0.001f;
    pidLastTime = now;

    float err = wrapAngle(refYaw - currentYaw);

    pidIntegral += err * dt;
    pidIntegral  = constrain(pidIntegral, -INTEGRAL_LIMIT, INTEGRAL_LIMIT);

    // Derivative on measurement (not error) to avoid kick on sudden
    // disturbances like wall collisions.  Negate because increasing yaw
    // means decreasing error.
    float yawDelta = wrapAngle(currentYaw - pidLastYaw);
    float deriv = -yawDelta / dt;
    pidLastYaw  = currentYaw;

    float headingCorr = KP * err + KI * pidIntegral + KD * deriv;
    headingCorr = constrain(headingCorr, -255.0f, 255.0f);

    // -- Base movement vector ------------------------------------------
    float fwd = 0.0f, right = 0.0f;
    switch (curDir) {
        case DIR_NORTH: fwd   =  1.0f; break;
        case DIR_SOUTH: fwd   = -1.0f; break;
        case DIR_EAST:  right =  1.0f; break;
        case DIR_WEST:  right = -1.0f; break;
        default: break;
    }

    // -- Lateral mix vector --------------------------------------------
    float latMix[NUM_MOTORS];
    getLateralMix(latMix);

    // -- Forward speed: PID-controlled, clamped to ±curSpeed -------------
    float effectiveSpeed = constrain(fwdCorrection,
                                     -(float)curSpeed, (float)curSpeed);
    float speedFrac = curSpeed / 255.0f;

    // -- Combine: movement + heading + centering -----------------------
    MotorState out[NUM_MOTORS];
    for (uint8_t i = 0; i < NUM_MOTORS; i++) {
        float val = fwd   * MIX_FWD[i]    * effectiveSpeed
                  + right * MIX_RIGHT[i]   * effectiveSpeed
                  + headingCorr            * MIX_ROT_CW[i]
                  + lateralCorrection      * latMix[i]      * speedFrac;
        int16_t clamped = constrain((int16_t)val, -255, 255);
        out[i].direction = (clamped < 0) ? 1 : 0;
        out[i].speed     = (uint8_t)abs(clamped);
    }

    motorsSetAll(out);
    return true;
}

void driveUpdateCentering(int16_t leftDist, int16_t rightDist) {
    if (!calibrated || curDir == DIR_STOP) {
        lateralCorrection = 0.0f;
        return;
    }

    // A reading above 100 means no wall nearby — treat as floating.
    bool leftValid  = leftDist  >= 0 && leftDist  <= 100;
    bool rightValid = rightDist >= 0 && rightDist <= 100;

    if (!leftValid && !rightValid) return;

    // Error: positive ⇒ drifted right relative to movement direction.
    float error;
    if (leftValid && rightValid) {
        // Both walls visible — aim for equal distance (center of lane)
        error = (float)(leftDist - rightDist);
    } else if (leftValid) {
        // Only left wall — hold calibrated distance from it
        error = 2.0f * (float)(leftDist - refLeft);
    } else {
        // Only right wall — hold calibrated distance from it
        error = -2.0f * (float)(rightDist - refRight);
    }

    // PID
    uint32_t now = millis();
    float dt = (centerLastTime == 0) ? 0.05f : (now - centerLastTime) / 1000.0f;
    if (dt < 0.001f) dt = 0.001f;
    centerLastTime = now;

    centerIntegral += error * dt;
    centerIntegral  = constrain(centerIntegral,
                                -CENTER_INTEGRAL_LIMIT, CENTER_INTEGRAL_LIMIT);

    float deriv = (error - centerLastErr) / dt;
    centerLastErr = error;

    // Negative sign: positive error (drifted right) → correct left
    float correction = -(CENTER_KP * error + CENTER_KI * centerIntegral
                         + CENTER_KD * deriv);
    lateralCorrection = constrain(correction, -255.0f, 255.0f);
}
