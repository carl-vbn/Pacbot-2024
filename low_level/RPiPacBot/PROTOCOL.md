# RPiPacBot UDP Binary Protocol

All communication uses UDP datagrams with raw binary payloads.
Byte order is **little-endian** (native to RP2350 and x86).

## Pi -> Server

### ALIVE (0x01) -- 5 bytes

Sent every second while idle (before sensor init completes).

| Offset | Size | Field      | Description              |
|--------|------|------------|--------------------------|
| 0      | 1    | type       | `0x01`                   |
| 1      | 4    | uptime_ms  | `uint32` millis() uptime |

### DEVICE_INFO (0x02) -- 4 bytes

Sent once after sensor initialisation completes.

| Offset | Size | Field        | Description                              |
|--------|------|--------------|------------------------------------------|
| 0      | 1    | type         | `0x02`                                   |
| 1      | 1    | sensor_mask  | Bitmask of present ToF slots (bit 0 = slot 0) |
| 2      | 1    | imu_present  | `1` if BNO055 found, `0` otherwise       |
| 3      | 1    | num_motors   | Number of motor channels (currently 4)   |

### SENSOR_DATA (0x03) -- variable length

Periodic sensor telemetry during logging.

| Offset | Size     | Field          | Description                                      |
|--------|----------|----------------|--------------------------------------------------|
| 0      | 1        | type           | `0x03`                                           |
| 1      | 4        | timestamp_ms   | `uint32` millis() when readings were taken        |
| 5      | 1        | sensor_count   | Number of `int16` distance readings that follow   |
| 6      | N*2      | distances      | `int16[N]` distance in mm per present sensor (-1 = error) |
| 6+N*2  | 1        | imu_flag       | `1` if Euler angles follow, `0` otherwise         |
| 7+N*2  | 12 (opt) | yaw,pitch,roll | `float32[3]` Euler angles in degrees (only if imu_flag=1) |

### LOG (0x04) -- 4 + L bytes

Free-form text log message.

| Offset | Size | Field    | Description                        |
|--------|------|----------|------------------------------------|
| 0      | 1    | type     | `0x04`                             |
| 1      | 1    | severity | `0`=DEBUG `1`=INFO `2`=WARN `3`=ERROR |
| 2      | 2    | length   | `uint16` byte length of text       |
| 4      | L    | text     | UTF-8 log message (not null-terminated) |

### PONG (0x05) -- 5 bytes

Response to a PING from the server.

| Offset | Size | Field     | Description              |
|--------|------|-----------|--------------------------|
| 0      | 1    | type      | `0x05`                   |
| 1      | 4    | uptime_ms | `uint32` millis() uptime |

## Server -> Pi

### CMD_START_LOG (0x11) -- 1 byte

Begin continuous sensor logging. Only valid after setup is complete (SETUP_DONE).

| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| 0      | 1    | type  | `0x11`      |

### CMD_SET_MOTOR (0x12) -- 4 bytes

Set a single motor. Accepted in SETUP_DONE and LOGGING states.

| Offset | Size | Field | Description                                     |
|--------|------|-------|-------------------------------------------------|
| 0      | 1    | type  | `0x12`                                          |
| 1      | 1    | index | Motor index 0-3                                 |
| 2      | 2    | speed | `int16` signed speed: positive=CW, negative=CCW, abs=PWM duty (0-255) |

### CMD_SET_MOTORS (0x15) -- 9 bytes

Set all 4 motors in one message. Accepted in SETUP_DONE and LOGGING states.

| Offset | Size | Field  | Description                                      |
|--------|------|--------|--------------------------------------------------|
| 0      | 1    | type   | `0x15`                                           |
| 1      | 2    | speed0 | `int16` motor 0 signed speed (-255..255)         |
| 3      | 2    | speed1 | `int16` motor 1 signed speed                     |
| 5      | 2    | speed2 | `int16` motor 2 signed speed                     |
| 7      | 2    | speed3 | `int16` motor 3 signed speed                     |

### CMD_SET_INTERVAL (0x13) -- 3 bytes

Change the sensor data send rate. Minimum 10 ms.

| Offset | Size | Field       | Description                    |
|--------|------|-------------|--------------------------------|
| 0      | 1    | type        | `0x13`                         |
| 1      | 2    | interval_ms | `uint16` new send interval ms  |

### CMD_STATUS (0x16) -- 1 byte

Request device info. The Pi responds with a fresh DEVICE_INFO message.

| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| 0      | 1    | type  | `0x16`      |

### CMD_PING (0x14) -- 1 byte

Latency probe. Pi responds with PONG.

| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| 0      | 1    | type  | `0x14`      |

### CMD_SET_DRIVE_MODE (0x17) -- 2 bytes

Switch between manual and cardinal-locked drive modes.

| Offset | Size | Field | Description                                  |
|--------|------|-------|----------------------------------------------|
| 0      | 1    | type  | `0x17`                                       |
| 1      | 1    | mode  | `0` = manual, `1` = cardinal locked           |

### CMD_CARDINAL_MOVE (0x18) -- 3 bytes

Move in a cardinal direction (cardinal-locked mode). Accepted in SETUP_DONE and LOGGING states.

| Offset | Size | Field     | Description                                       |
|--------|------|-----------|---------------------------------------------------|
| 0      | 1    | type      | `0x18`                                            |
| 1      | 1    | direction | `0`=stop `1`=north `2`=east `3`=south `4`=west    |
| 2      | 1    | speed     | `uint8` speed 0-255                                |

### CMD_CALIBRATE (0x19) -- 1 byte

Set the current heading as north for cardinal-locked mode.

| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| 0      | 1    | type  | `0x19`      |

### CMD_STOP_LOG (0x1A) -- 1 byte

Stop continuous sensor logging. Returns to SETUP_DONE state.

| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| 0      | 1    | type  | `0x1A`      |

## State Machine

```
  IDLE --[init done]--> SETUP_DONE --[CMD_START_LOG]--> LOGGING
   |                        ^                             |
   | (alive heartbeats)     |                             |
                            +-------[CMD_STOP_LOG]--------+
```

- **IDLE**: Pi sends ALIVE every second while sensors initialise.
- **SETUP_DONE**: Pi sends DEVICE_INFO once. Server may send CMD_START_LOG.
- **LOGGING**: Pi sends SENSOR_DATA at the configured interval. CMD_STOP_LOG returns to SETUP_DONE.

Motor commands (CMD_SET_MOTOR, CMD_SET_MOTORS), drive mode (CMD_SET_DRIVE_MODE), cardinal movement (CMD_CARDINAL_MOVE), calibration (CMD_CALIBRATE), status (CMD_STATUS), and pings (CMD_PING) are accepted in SETUP_DONE and LOGGING states.
