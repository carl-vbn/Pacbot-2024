#pragma once

#include <Arduino.h>
#include "config.h"

// =====================================================================
//  Binary UDP protocol -- see PROTOCOL.md for full specification
// =====================================================================

// -- Message types: Pi -> Server --------------------------------------
#define MSG_ALIVE       0x01
#define MSG_DEVICE_INFO 0x02
#define MSG_SENSOR_DATA 0x03
#define MSG_LOG         0x04
#define MSG_PONG        0x05

// -- Message types: Server -> Pi --------------------------------------
#define CMD_SETUP       0x10
#define CMD_START_LOG   0x11
#define CMD_SET_MOTOR    0x12
#define CMD_SET_MOTORS   0x15
#define CMD_SET_INTERVAL 0x13
#define CMD_PING         0x14

// -- Log severity levels -----------------------------------------------
#define LOG_DEBUG   0
#define LOG_INFO    1
#define LOG_WARN    2
#define LOG_ERROR   3

// -- Maximum packet sizes (bytes) --------------------------------------
// ALIVE:       1 type + 4 uptime = 5
// DEVICE_INFO: 1 type + 1 sensor_mask + 1 imu + 1 num_motors = 4
// SENSOR_DATA: 1 type + 4 timestamp + 1 sensor_count
//              + MAX_SENSORS*2 distances + 1 imu_flag + 12 euler = 34
// LOG:         1 type + 1 severity + 2 length + N text
#define MAX_PACKET_SIZE 256

// -- Packet builders (return length written into buf) -----------------

// ALIVE: 5 bytes
inline uint8_t buildAlive(uint8_t *buf, uint32_t uptimeMs) {
    buf[0] = MSG_ALIVE;
    memcpy(buf + 1, &uptimeMs, 4);
    return 5;
}

// DEVICE_INFO: 4 bytes
inline uint8_t buildDeviceInfo(uint8_t *buf, uint8_t sensorMask, bool imu) {
    buf[0] = MSG_DEVICE_INFO;
    buf[1] = sensorMask;
    buf[2] = imu ? 1 : 0;
    buf[3] = NUM_MOTORS;
    return 4;
}

// SENSOR_DATA: variable length
// readings[] has one entry per *present* sensor, in slot order.
// count = number of present sensors.
inline uint8_t buildSensorData(uint8_t *buf,
                               uint32_t timestampMs,
                               uint8_t sensorCount,
                               const int16_t *readings,
                               bool hasImu,
                               float yaw, float pitch, float roll) {
    uint8_t pos = 0;
    buf[pos++] = MSG_SENSOR_DATA;
    memcpy(buf + pos, &timestampMs, 4); pos += 4;
    buf[pos++] = sensorCount;

    for (uint8_t i = 0; i < sensorCount; i++) {
        memcpy(buf + pos, &readings[i], 2); pos += 2;
    }

    buf[pos++] = hasImu ? 1 : 0;
    if (hasImu) {
        memcpy(buf + pos, &yaw,   4); pos += 4;
        memcpy(buf + pos, &pitch, 4); pos += 4;
        memcpy(buf + pos, &roll,  4); pos += 4;
    }

    return pos;
}

// LOG: 4 + textLen bytes
inline uint8_t buildLog(uint8_t *buf, uint8_t severity,
                        const char *text, uint16_t textLen) {
    buf[0] = MSG_LOG;
    buf[1] = severity;
    memcpy(buf + 2, &textLen, 2);
    memcpy(buf + 4, text, textLen);
    return 4 + textLen;
}

// PONG: 5 bytes
inline uint8_t buildPong(uint8_t *buf, uint32_t uptimeMs) {
    buf[0] = MSG_PONG;
    memcpy(buf + 1, &uptimeMs, 4);
    return 5;
}
