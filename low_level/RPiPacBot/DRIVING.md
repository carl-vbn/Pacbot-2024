# RPiPacBot Driving Mechanism

This document describes the full driving pipeline implemented in `drive.cpp`
and orchestrated from `RPiPacBot.ino`. It covers every control loop, every
piece of state, every constant, and the edge cases you need to be aware of.

## 1. Big picture

The robot is a four-wheel omnidrive. Core 0 handles sensing, fusion, and motor
output; Core 1 handles WiFi/UDP (see `comms.cpp`). All command traffic and
runtime tuning (PID gains, sensor offsets, drive mode, cardinal moves,
calibration) enters via the shared-data mutex and is polled from Core 0's main
loop.

Two drive modes exist:

| Mode                  | Who decides motor outputs                                      |
|-----------------------|----------------------------------------------------------------|
| `DRIVE_MANUAL`        | The server directly. Core 0 forwards raw motor commands.       |
| `DRIVE_CARDINAL_LOCKED` | Core 0's control loops. Server only sends direction + speed. |

In MANUAL mode `drive.cpp` is effectively bypassed; the rest of this document
covers CARDINAL_LOCKED.

## 2. Coordinate conventions

Cardinal directions are defined in `drive.h`:

```
DIR_STOP = 0, DIR_NORTH = 1, DIR_EAST = 2, DIR_SOUTH = 3, DIR_WEST = 4
```

"North" is the robot's forward-facing direction at calibration time. Sensor
slots are mapped to cardinals in `config.h`:

```
SENSOR_IDX_NORTH = 0
SENSOR_IDX_EAST  = 3
SENSOR_IDX_SOUTH = 1
SENSOR_IDX_WEST  = 2
```

This mapping never changes at runtime.

## 3. Motor mixing vectors

`drive.cpp` defines three orthogonal mixing vectors over the four motors. They
must match `motor_config.json` used by the dashboard for manual mode.

```
MIX_FWD    = {  1,  0,  0, -1 }   // pure +Y (north) motion
MIX_RIGHT  = {  0, -1, -1,  0 }   // pure +X (east) motion
MIX_ROT_CW = { -1, -1,  1, -1 }   // pure clockwise rotation (yaw)
```

Any desired motion is a linear combination of these three. All final per-motor
values are clamped to `int16_t [-255, 255]`; sign becomes the CW/CCW flag, the
absolute value becomes the PWM duty.

## 4. State variables (`drive.cpp`)

```
// Mode / command
DriveMode   mode            // DRIVE_MANUAL or DRIVE_CARDINAL_LOCKED
float       refYaw          // heading captured on mode entry / calibrate
CardinalDir curDir          // currently commanded cardinal direction
uint8_t     curSpeed        // 0..255 PWM magnitude cap

// Heading PID
float       pidIntegral
float       pidLastYaw      // derivative-on-measurement: needs last yaw
uint32_t    pidLastTime

// Centering
bool        calibrated      // driveCalibrate() has been called
int16_t     refLeft, refRight  // distances captured at calibration
float       centerIntegral
float       centerLastErr
uint32_t    centerLastTime
float       lateralCorrection  // PID output, consumed by driveUpdate
bool        suppressHeading    // latched by centering when a perpendicular wall is very close

// Forward braking
float       fwdCorrection   // 0..curSpeed target along movement axis
```

Reset points:

- `driveInit()` on boot: MANUAL, uncalibrated, all integrators zeroed.
- `driveSetMode(DRIVE_CARDINAL_LOCKED, yaw)`: captures `refYaw`, resets heading
  PID, stops the robot (`curDir = STOP`, `curSpeed = 0`), zeroes
  `lateralCorrection`, `fwdCorrection`, and `suppressHeading`.
- `driveSetMode(DRIVE_MANUAL, _)`: calls `motorsStop()`.
- `driveCalibrate()`: captures `refYaw`, `refLeft`, `refRight`; sets
  `calibrated = true`; resets both PID integrators and forward braking.
- `driveSetCardinal()` with a new direction: resets **centering PID** and
  forward braking (because the "lateral" axis has changed) but does **not**
  reset the heading PID.

## 5. Heading PID

File: `drive.cpp`, `driveUpdate(float currentYaw)`.

Purpose: keep the robot's heading locked to `refYaw` despite slip, bumps, or
external forces. Runs at ~100 Hz (see `DRIVE_UPDATE_MS = 10` in
`RPiPacBot.ino`).

Gains (mutable — `driveSetPidGains(PID_LOOP_HEADING, ...)` is wired to the
dashboard's PID panel):

```
KP = 2.0   KI = 0.05   KD = 0.1
INTEGRAL_LIMIT = 100.0   (anti-windup clamp on pidIntegral)
```

Algorithm:

1. Compute `dt` from `millis()` with a floor of 1 ms to avoid derivative
   explosions on tight loop ticks.
2. `err = wrapAngle(refYaw − currentYaw)` — `wrapAngle` keeps the error in
   `[-180°, 180°)` so the PID takes the short way around.
3. **Derivative on measurement.** `deriv = -(yaw − pidLastYaw) / dt`. This
   is intentionally *not* derivative of error: a sudden shove on the chassis
   creates a big `err` step, but the measurement itself changes smoothly, so
   the D term stays well-behaved. Sign flip because rising yaw means falling
   error.
4. If `suppressHeading` is true (see §7), skip the integral accumulation and
   output zero — but still update `pidLastYaw` and `pidLastTime` so the next
   un-suppressed tick has fresh state.
5. Otherwise accumulate: `pidIntegral += err * dt`, clamp to
   `±INTEGRAL_LIMIT`. Output
   `headingCorr = KP*err + KI*pidIntegral + KD*deriv`, then clamp to
   `±50`.

The ±50 output clamp (tightened from ±255) intentionally limits how much
authority heading can take over lateral motion. Headroom in the final motor
clamp (±255) is reserved for translational speed and centering.

## 6. Lateral centering PID

File: `drive.cpp`, `driveUpdateCentering(int16_t leftDist, int16_t rightDist)`.

Purpose: keep the robot in the middle of a corridor, or a calibrated offset
from a single wall. Runs at ~20 Hz (see `CENTER_UPDATE_MS = 50`).

Inputs are the two perpendicular ToF readings (see `driveGetLateralSensors()`
which maps `curDir` to a pair of sensor slots). The readings are already
offset-corrected inside `sensorReadMM`.

Gains (mutable — `driveSetPidGains(PID_LOOP_CENTERING, ...)`):

```
CENTER_KP = 1.0   CENTER_KI = 0.0   CENTER_KD = 0.2
CENTER_INTEGRAL_LIMIT = 80.0
```

Validity rule:

```
leftValid  = leftDist  in [0, 100]
rightValid = rightDist in [0, 100]
```

Readings above 100 mm or equal to −1 are treated as "no wall" and excluded.
See §11 for how this can silently disable centering.

Error computation:

| Case                    | Error                                 | Meaning                                   |
|-------------------------|---------------------------------------|-------------------------------------------|
| Both walls visible      | `leftDist − rightDist`                | Positive ⇒ drifted right, correct left.   |
| Only left wall visible  | `2 * (leftDist − refLeft)`            | Hold calibrated standoff from left wall.  |
| Only right wall visible | `-2 * (rightDist − refRight)`         | Hold calibrated standoff from right wall. |
| Neither valid           | (early return, `lateralCorrection` unchanged) | No update.                        |

PID:

1. `dt` from `millis()`, floor 1 ms.
2. `centerIntegral += err * dt`, clamp to `±CENTER_INTEGRAL_LIMIT`.
3. `deriv = (err − centerLastErr) / dt` (derivative on error — unlike heading,
   a mechanical disturbance here *should* provoke a reaction).
4. `correction = -(CENTER_KP*err + CENTER_KI*centerIntegral + CENTER_KD*deriv)`,
   clamped to `±255` → written to `lateralCorrection`.

The negative sign makes the output "push right" when `error > 0` (drifted
right ⇒ correct back left ⇒ but we *store* a value that is then rotated into
motor space by `getLateralMix()`, which depends on `curDir`).

## 7. Heading suppression near a wall

`driveUpdateCentering` also maintains `suppressHeading`:

```
HEADING_SUPPRESS_DIST_MM = 40   // mm

suppressHeading = (leftValid  && leftDist  <= 40) ||
                  (rightValid && rightDist <= 40);
```

When set, `driveUpdate` zeroes the heading correction and skips integral
accumulation. Rationale: if a perpendicular wall is within 40 mm, the heading
PID may fight the lateral PID (IMU says "go straight" while ToF says "escape
this wall"). Letting centering alone steer makes the robot reliably peel off
the wall instead of scraping along it.

`suppressHeading` is cleared to `false` in `driveInit`, on `DRIVE_CARDINAL_LOCKED`
entry, and whenever `curDir == DIR_STOP`.

Important: the flag is only updated at the 20 Hz centering cadence, so the
heading loop can run up to ~50 ms on stale suppression state. In practice
this is negligible.

## 8. Forward braking (replaces forward PID)

File: `drive.cpp`, `driveUpdateForward(int16_t forwardDist)`. Runs at the same
20 Hz cadence as centering.

This loop used to be a full PID targeting a set wall distance. It now uses
**simple thresholds** — a three-zone look-up:

```
FWD_SLOW_DIST_MM = 249   // enter slow zone at this distance (mm)
FWD_STOP_DIST_MM =  70   // stop below this distance (mm)
FWD_SLOW_SPEED   =  70   // speed cap inside the slow zone (0-255)
```

Logic (reads `curSpeed`, writes `fwdCorrection`):

```
if forwardDist <= FWD_STOP_DIST_MM:  fwdCorrection = 0
elif forwardDist <= FWD_SLOW_DIST_MM: fwdCorrection = FWD_SLOW_SPEED
else:                                 fwdCorrection = curSpeed
forwardDist == -1 (sensor error):     keep last fwdCorrection
```

Properties:

- **Never negative.** The robot does not back off if it ends up closer than
  `FWD_STOP_DIST_MM`; it just stops. This is intentional — earlier PID
  behaviour could reverse into obstacles.
- `fwdCorrection` never exceeds `curSpeed` (`driveUpdate`'s
  `constrain(fwdCorrection, -curSpeed, curSpeed)` also enforces this, so if
  `curSpeed < FWD_SLOW_SPEED` the robot keeps going at `curSpeed`).
- UI-facing PID dials for "Forward" are still honoured by the dashboard and
  the protocol (`CMD_SET_PID` loop 2); the firmware silently ignores the
  values (`driveSetPidGains` case `PID_LOOP_FORWARD` does nothing). The dials
  are kept in case PID is reintroduced.

## 9. Output combination (`driveUpdate`)

Once `headingCorr`, `lateralCorrection`, and `fwdCorrection` are computed,
the per-motor command is:

```
// Desired body motion (unit vector along the cardinal axis)
fwd, right = {NORTH: (1,0), SOUTH: (-1,0), EAST: (0,1), WEST: (0,-1), STOP: (0,0)}[curDir]

// Forward braking output, clamped to the currently allowed speed envelope
effectiveSpeed = constrain(fwdCorrection, -curSpeed, curSpeed)

// Centering authority scales with commanded speed so it doesn't dominate slow driving
speedFrac = curSpeed / 255

// latMix[i] is MIX_RIGHT (or -MIX_RIGHT) for N/S, MIX_FWD (or -MIX_FWD) for E/W
latMix = getLateralMix(curDir)

for each motor i:
    val = fwd   * MIX_FWD[i]     * effectiveSpeed
        + right * MIX_RIGHT[i]   * effectiveSpeed
        + headingCorr            * MIX_ROT_CW[i]
        + lateralCorrection      * latMix[i]      * speedFrac
    motor[i] = clamp(val, -255, 255)
```

Points worth noting:

- Heading correction is **not** scaled by `speedFrac`. It is an *absolute*
  yaw-rotation term — the robot can spin in place to fix heading even at
  `curSpeed = 0`. This is why the ±50 clamp in §5 matters: it prevents the
  heading term from swamping translation.
- Centering **is** scaled by `speedFrac`. When the robot is moving slowly,
  lateral pushes are correspondingly gentle, which avoids overshoot.
- Forward braking works by shrinking `effectiveSpeed`, not by flipping its
  sign — combined with the `-curSpeed` lower bound of the `constrain`, the
  robot cannot be driven backwards by braking alone.

## 10. Calibration

File: `drive.cpp`, `driveCalibrate(yaw, leftDist, rightDist)`. Invoked from
`RPiPacBot.ino` when `CMD_CALIBRATE` arrives.

Captures:

- `refYaw = current yaw`
- `refLeft = sensor[WEST]`, `refRight = sensor[EAST]` (hardcoded to the
  west/east sensors regardless of current heading — the server sends
  `CMD_CALIBRATE` when the robot is posed with its "forward" along north)
- Sets `calibrated = true`
- Resets all integrators and forward braking state

**Calibration is a prerequisite for single-wall centering.** With both walls
visible, the error uses `leftDist − rightDist` and needs no calibration, but
when only one wall is in range the error is measured against `refLeft`
(resp. `refRight`). See §11 for the failure modes this creates.

## 11. Known constraints and gotchas

### a) Centering silently does nothing when `!calibrated`

```
if (!calibrated) {
    lateralCorrection = 0.0f;
    return;
}
```

**Important consequence:** combined with §7, an uncalibrated robot rolling
toward a wall will have its heading PID *suppressed* below 40 mm and its
centering PID *disabled* by the missing calibration. It will then drift
freely into the wall.

Always press CALIBRATE once per boot before driving in cardinal mode.

### b) Bad calibration pose bakes in a bad target

In the single-wall branch the error is `2 * (dist − refWall)`. If you
calibrated the robot while it was already close to a wall (`refLeft = 20`),
then returning to 20 mm from that wall produces `error = 0` and no
correction, even with a large `CENTER_KP`. Symptom: the robot "hugs" the
wall it was calibrated next to.

### c) The 100 mm "floating" cutoff is aggressive

Readings above 100 mm are treated as "no wall." In a corridor wider than
~200 mm, the far wall routinely flips in and out of range, dropping the
robot into single-wall (and therefore calibration-dependent) behaviour.
When *both* readings exceed 100 mm, centering early-returns and
`lateralCorrection` keeps its last value — often zero.

### d) Sensor offsets can push real readings out of range

`sensorReadMM` adds `sensorOffsets[i]` to the raw reading. Negative-
producing offsets are clamped at 0 (not −1), but positive offsets can move
a genuine 95 mm reading to above 100 mm and thus silence that sensor for
centering purposes.

### e) `suppressHeading` lag

Heading suppression is re-evaluated only every ~50 ms. Between checks the
heading PID can briefly run while the robot is already within 40 mm of a
wall. Not a correctness issue; just don't expect instantaneous hand-off.

### f) Reset on direction change

`driveSetCardinal(newDir, speed)` with `newDir != curDir` wipes centering
and forward-braking state. This is deliberate (the "left" and "right"
sensors now point at different walls), but it means integral history
does not carry through a turn.

## 12. Timing summary

| Loop                         | Cadence  | Source                           |
|------------------------------|----------|----------------------------------|
| Heading PID (`driveUpdate`)  | ~100 Hz  | `DRIVE_UPDATE_MS = 10`           |
| Centering + forward braking  | ~20 Hz   | `CENTER_UPDATE_MS = 50`          |
| Sensor logging (STATE_LOGGING)| ~10 Hz  | `READ_INTERVAL_MS = 100` in `config.h` |
| Command/poll layer           | every loop iteration | mutex polls are cheap |

The rate caps exist because the underlying sensors have their own internal
sample rates (VL6180X ≈30–60 Hz, BNO055 fusion ≈100 Hz). Running faster only
adds I²C bus pressure and amplifies PID-derivative noise.

## 13. Invocation from `RPiPacBot.ino`

Every loop iteration on Core 0, in this order:

1. `commsPollDriveMode()` — honor MANUAL/CARDINAL switches.
2. `commsPollCalibrate()` — if pending, read IMU + west/east sensors and
   call `driveCalibrate`.
3. `commsPollPidGains()` — if pending, push to `driveSetPidGains`.
4. `commsPollSensorOffsets()` — if pending, push to `sensorsSetOffset`.
5. `commsPollMotorCmd()` — raw motor command force-switches to MANUAL and
   drives `motorsSetAll` directly.
6. If MANUAL: done for this iteration.
7. If CARDINAL_LOCKED:
   - `commsPollCardinalCmd()` → `driveSetCardinal(dir, speed)`.
   - Every `DRIVE_UPDATE_MS`: read IMU, call `driveUpdate(yaw)`.
   - Every `CENTER_UPDATE_MS`: read lateral ToF pair → `driveUpdateCentering`;
     read forward ToF → `driveUpdateForward`.
8. If `commsGetState() == STATE_LOGGING`: every `READ_INTERVAL_MS` read all
   present sensors + IMU and push via `commsPostSensorData`.

Note that **all** sensor reads go through `sensorReadMM`, which applies the
per-slot offset. This means PID loops, braking, logging, and calibration all
see the same calibrated values.

## 14. Tunables at a glance

All in `drive.cpp` unless otherwise noted.

| Symbol                        | Default | Role                                                        |
|-------------------------------|---------|-------------------------------------------------------------|
| `KP`, `KI`, `KD`              | 2.0, 0.05, 0.1 | Heading PID (UI-tunable)                             |
| `INTEGRAL_LIMIT`              | 100.0   | Anti-windup for heading integrator                          |
| Heading output clamp          | ±50     | Hard cap on `headingCorr` (hardcoded in `driveUpdate`)      |
| `CENTER_KP/KI/KD`             | 1.0, 0.0, 0.2 | Centering PID (UI-tunable)                            |
| `CENTER_INTEGRAL_LIMIT`       | 80.0    | Anti-windup for centering integrator                        |
| 100 mm floating cutoff        | 100     | Readings above this are "no wall" for centering             |
| `HEADING_SUPPRESS_DIST_MM`    | 40      | Perpendicular-wall proximity that disables heading PID      |
| `FWD_SLOW_DIST_MM`            | 249     | Enter slow zone below this forward distance                 |
| `FWD_STOP_DIST_MM`            | 70      | Stop below this forward distance                            |
| `FWD_SLOW_SPEED`              | 70      | PWM cap in the slow zone (0–255)                            |
| `DRIVE_UPDATE_MS`             | 10      | Heading PID period (in `RPiPacBot.ino`)                     |
| `CENTER_UPDATE_MS`            | 50      | Centering + forward braking period (in `RPiPacBot.ino`)     |
| `READ_INTERVAL_MS`            | 100     | Sensor logging period (in `config.h`)                       |

PID dials for the Forward loop are still present in the dashboard and
protocol for ergonomic reasons, but they have no effect — see §8.
