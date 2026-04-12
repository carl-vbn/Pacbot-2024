# RPiPacBot UDP Binary Protocol

All communication uses UDP datagrams with raw binary payloads.
Byte order is **little-endian** (native to RP2350 and x86).

## Pi -> Server

### ALIVE (0x01) -- 5 bytes

Sent every second while waiting for the setup command.

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

### CMD_SETUP (0x10) -- 1 byte

Instructs the Pi to initialise sensors. Only valid in IDLE state.

| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| 0      | 1    | type  | `0x10`      |

### CMD_START_LOG (0x11) -- 1 byte

Begin continuous sensor logging. Only valid after setup is complete.

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

### CMD_RESCAN_SENSORS (0x16) -- 1 byte

Re-initialise all ToF sensors and the IMU at runtime. Accepted in SETUP_DONE and LOGGING states. The Pi will power-cycle all sensor CE lines, re-run the init sequence, and send a fresh DEVICE_INFO with the updated sensor mask.

| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| 0      | 1    | type  | `0x16`      |

### CMD_PING (0x14) -- 1 byte

Latency probe. Pi responds with PONG.

| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| 0      | 1    | type  | `0x14`      |

## State Machine

```
  IDLE  --[CMD_SETUP]--> SETUP_REQ --[init done]--> SETUP_DONE --[CMD_START_LOG]--> LOGGING
   |                                                     |
   | (alive heartbeats)                    (device_info sent)
```

- **IDLE**: Pi sends ALIVE every second. Server should send CMD_SETUP when ready.
- **SETUP_REQ**: Core 0 runs sensor/IMU init. No packets sent.
- **SETUP_DONE**: Pi sends DEVICE_INFO once. Server should send CMD_START_LOG.
- **LOGGING**: Pi sends SENSOR_DATA at the configured interval.

Motor commands (CMD_SET_MOTOR, CMD_SET_MOTORS) and pings (CMD_PING) are accepted after setup completes (SETUP_DONE and LOGGING).
