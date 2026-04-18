#include "drive.h"
#include <string.h>

// -- Motor mixing vectors (must match motor_config.json) ---------------
static const float MIX_FWD[NUM_MOTORS]    = {  1,  0,  0, -1 };
static const float MIX_RIGHT[NUM_MOTORS]  = {  0, -1, -1,  0 };
static const float MIX_ROT_CW[NUM_MOTORS] = { -1, -1,  1, -1 };

// -- Heading PID gains (mutable for remote tuning) --------------------
static float KP = 2.0f;
static float KI = 0.05f;
static float KD = 0.1f;
static const float INTEGRAL_LIMIT = 100.0f;

// -- Centering PID gains (mutable for remote tuning) ------------------
static float CENTER_KP = 1.0f;
static float CENTER_KI = 0.0f;
static float CENTER_KD = 0.2f;
static const float CENTER_INTEGRAL_LIMIT = 80.0f;

// If either perpendicular (lateral) sensor reads closer than this, the
// heading PID is suspended so only lateral centering fights the drift.
static const int16_t HEADING_SUPPRESS_DIST_MM = 40;

// -- Forward distance braking (simple two-threshold, replaces PID) ----
// > FWD_SLOW_DIST_MM        : use commanded speed
// [stop, FWD_SLOW_DIST_MM]  : capped at FWD_SLOW_SPEED
// < FWD_STOP_DIST_MM        : stopped
// Robot does NOT back off when too close.
static const int16_t FWD_SLOW_DIST_MM = 190;  // start slowing (mm)
static const int16_t FWD_STOP_DIST_MM = 70;   // stop (mm)
static const uint8_t FWD_SLOW_SPEED   = 40;   // speed cap in slow zone (0-255)
static const float   FWD_ACCEL_RATE   = 500.0f;  // speed-units per second

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
static bool        suppressHeading   = false;  // set by driveUpdateCentering when a perpendicular wall is very close

// Forward braking output (consumed by driveUpdate)
static float       fwdCorrection     = 0.0f;
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
    suppressHeading  = false;
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
        fwdLastTime       = 0;
        suppressHeading   = false;
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
    pidLastYaw        = currentYaw;
    pidLastTime       = millis();
    centerIntegral    = 0.0f;
    centerLastErr     = 0.0f;
    centerLastTime    = 0;
    lateralCorrection = 0.0f;
    fwdCorrection     = 0.0f;
    fwdLastTime       = 0;
}

void driveSetCardinal(CardinalDir dir, uint8_t speed) {
    if (dir != curDir) {
        // Axes changed — reset centering state and forward braking
        centerIntegral    = 0.0f;
        centerLastErr     = 0.0f;
        centerLastTime    = 0;
        lateralCorrection = 0.0f;
        fwdCorrection     = 0.0f;
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
    if (forwardDist < 0) return;  // sensor error — keep last value

    float target;
    if (forwardDist <= FWD_STOP_DIST_MM) {
        target = 0.0f;
    } else if (forwardDist <= FWD_SLOW_DIST_MM) {
        // In slow zone: cap at FWD_SLOW_SPEED (driveUpdate's constrain
        // further clamps to curSpeed, so this never speeds up the robot).
        target = (float)FWD_SLOW_SPEED;
    } else {
        // Clear ahead — use commanded speed
        target = (float)curSpeed;
    }

    uint32_t now = millis();
    float dt = (fwdLastTime == 0) ? 0.05f : (now - fwdLastTime) / 1000.0f;
    if (dt < 0.001f) dt = 0.001f;
    fwdLastTime = now;

    float maxStep = FWD_ACCEL_RATE * dt;
    float delta   = target - fwdCorrection;
    if (delta >  maxStep) delta =  maxStep;
    if (delta < -maxStep) delta = -maxStep;
    fwdCorrection += delta;
}

bool driveUpdate(float currentYaw) {
    if (mode != DRIVE_CARDINAL_LOCKED) return false;

    // -- Heading PID ---------------------------------------------------
    uint32_t now = millis();
    float dt = (pidLastTime == 0) ? 0.01f : (now - pidLastTime) / 1000.0f;
    if (dt < 0.001f) dt = 0.001f;
    pidLastTime = now;

    float err = wrapAngle(refYaw - currentYaw);

    // Derivative on measurement (not error) to avoid kick on sudden
    // disturbances like wall collisions.  Negate because increasing yaw
    // means decreasing error.
    float yawDelta = wrapAngle(currentYaw - pidLastYaw);
    float deriv = -yawDelta / dt;
    pidLastYaw  = currentYaw;

    // When a perpendicular wall is very close, hand off entirely to the
    // lateral centering loop — no heading correction, no integral windup.
    float headingCorr = 0.0f;
    if (!suppressHeading) {
        pidIntegral += err * dt;
        pidIntegral  = constrain(pidIntegral, -INTEGRAL_LIMIT, INTEGRAL_LIMIT);
        headingCorr = KP * err + KI * pidIntegral + KD * deriv;
        headingCorr = constrain(headingCorr, -50.0f, 50.0f);
    }

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

    // -- Forward speed: (no longer) PID-controlled, clamped to ±curSpeed -------------
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
    if (curDir == DIR_STOP) {
        lateralCorrection = 0.0f;
        suppressHeading   = false;
        return;
    }

    // A reading above 100 means no wall nearby — treat as floating.
    bool leftValid  = leftDist  >= 0 && leftDist  <= 100;
    bool rightValid = rightDist >= 0 && rightDist <= 100;

    // Suppress heading PID if a perpendicular wall is within
    // HEADING_SUPPRESS_DIST_MM — let centering alone handle the drift.
    suppressHeading = (leftValid  && leftDist  <= HEADING_SUPPRESS_DIST_MM) ||
                      (rightValid && rightDist <= HEADING_SUPPRESS_DIST_MM);

    if (!calibrated) {
        lateralCorrection = 0.0f;
        return;
    }

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

void driveSetPidGains(uint8_t loop, float kp, float ki, float kd) {
    switch (loop) {
        case PID_LOOP_HEADING:
            KP = kp; KI = ki; KD = kd;
            break;
        case PID_LOOP_CENTERING:
            CENTER_KP = kp; CENTER_KI = ki; CENTER_KD = kd;
            break;
        case PID_LOOP_FORWARD:
            // Forward braking no longer uses PID — see driveUpdateForward,
            // which uses simple distance thresholds instead. The UI dials
            // are kept in case PID is reintroduced later; for now the
            // gains are silently ignored.
            (void)kp; (void)ki; (void)kd;
            break;
    }
}
