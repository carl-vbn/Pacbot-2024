#include "comms.h"

#include <WiFi.h>
#include <WiFiUdp.h>

// -- Shared state ------------------------------------------------------
SharedData commsShared;
mutex_t    commsSharedMutex;

// -- Core-1 local state ------------------------------------------------
static WiFiUDP       udp;
static IPAddress     serverIP;
static uint8_t       txBuf[MAX_PACKET_SIZE];
static uint8_t       rxBuf[MAX_PACKET_SIZE];
static uint32_t      lastAliveSent  = 0;
static uint32_t      lastDataSent   = 0;
static bool          deviceInfoSent = false;

// -- Helpers -----------------------------------------------------------

static void sendPacket(const uint8_t *buf, uint8_t len) {
    udp.beginPacket(serverIP, SERVER_PORT);
    udp.write(buf, len);
    udp.endPacket();
}

static void sendLogMsg(uint8_t severity, const char *text) {
    uint16_t tlen = strlen(text);
    if (tlen > MAX_PACKET_SIZE - 4) tlen = MAX_PACKET_SIZE - 4;
    uint8_t len = buildLog(txBuf, severity, text, tlen);
    sendPacket(txBuf, len);
}

// -- Inbound command handling ------------------------------------------

static void handleRx() {
    int packetSize = udp.parsePacket();
    while (packetSize > 0) {
        int n = udp.read(rxBuf, sizeof(rxBuf));
        if (n < 1) { packetSize = udp.parsePacket(); continue; }

        uint8_t type = rxBuf[0];

        switch (type) {
            case CMD_SETUP: {
                RobotState cur = commsGetState();
                if (cur == STATE_IDLE) {
                    commsSetState(STATE_SETUP_REQ);
                }
                break;
            }

            case CMD_START_LOG: {
                RobotState cur = commsGetState();
                if (cur == STATE_SETUP_DONE) {
                    commsSetState(STATE_LOGGING);
                }
                break;
            }

            case CMD_SET_MOTOR: {
                RobotState cur = commsGetState();
                if (n >= 4 && (cur == STATE_SETUP_DONE || cur == STATE_LOGGING)) {
                    uint8_t idx = rxBuf[1];
                    int16_t speed;
                    memcpy(&speed, rxBuf + 2, 2);
                    if (idx < NUM_MOTORS) {
                        mutex_enter_blocking(&commsSharedMutex);
                        commsShared.motorCmd[idx].direction = (speed < 0) ? 1 : 0;
                        commsShared.motorCmd[idx].speed     = (uint8_t)min(abs(speed), 255);
                        commsShared.motorCmdPending = true;
                        mutex_exit(&commsSharedMutex);
                    }
                }
                break;
            }

            case CMD_SET_MOTORS: {
                RobotState cur = commsGetState();
                if (n >= 1 + NUM_MOTORS * 2 && (cur == STATE_SETUP_DONE || cur == STATE_LOGGING)) {
                    mutex_enter_blocking(&commsSharedMutex);
                    for (uint8_t i = 0; i < NUM_MOTORS; i++) {
                        int16_t speed;
                        memcpy(&speed, rxBuf + 1 + i * 2, 2);
                        commsShared.motorCmd[i].direction = (speed < 0) ? 1 : 0;
                        commsShared.motorCmd[i].speed     = (uint8_t)min(abs(speed), 255);
                    }
                    commsShared.motorCmdPending = true;
                    mutex_exit(&commsSharedMutex);
                }
                break;
            }

            case CMD_SET_INTERVAL: {
                if (n >= 3) {
                    uint16_t interval;
                    memcpy(&interval, rxBuf + 1, 2);
                    if (interval >= 10) {  // clamp minimum
                        mutex_enter_blocking(&commsSharedMutex);
                        commsShared.sendIntervalMs = interval;
                        mutex_exit(&commsSharedMutex);
                    }
                }
                break;
            }

            case CMD_PING: {
                uint8_t len = buildPong(txBuf, millis());
                sendPacket(txBuf, len);
                break;
            }
        }

        packetSize = udp.parsePacket();
    }
}

// -- Outbound data sending ---------------------------------------------

static void sendAlive() {
    uint8_t len = buildAlive(txBuf, millis());
    sendPacket(txBuf, len);
}

static void sendDeviceInfo() {
    uint8_t mask;
    bool    hasImu;

    mutex_enter_blocking(&commsSharedMutex);
    mask   = commsShared.sensorMask;
    hasImu = commsShared.hasImu;
    mutex_exit(&commsSharedMutex);

    uint8_t len = buildDeviceInfo(txBuf, mask, hasImu);
    sendPacket(txBuf, len);
}

static void sendSensorData() {
    int16_t  readings[MAX_SENSORS];
    uint8_t  count;
    bool     hasImu;
    float    y, p, r;
    uint32_t ts;

    mutex_enter_blocking(&commsSharedMutex);
    count  = commsShared.sensorCount;
    hasImu = commsShared.hasImu;
    y = commsShared.yaw;
    p = commsShared.pitch;
    r = commsShared.roll;
    ts = commsShared.dataTimestamp;
    memcpy(readings, commsShared.sensorReadings, count * sizeof(int16_t));
    mutex_exit(&commsSharedMutex);

    uint8_t len = buildSensorData(txBuf, ts, count, readings, hasImu, y, p, r);
    sendPacket(txBuf, len);
}

// -- Core 0 helpers ----------------------------------------------------

void commsPostSensorData(uint8_t count, uint8_t mask, const int16_t *readings,
                         bool hasImu, float yaw, float pitch, float roll) {
    mutex_enter_blocking(&commsSharedMutex);
    commsShared.sensorCount = count;
    commsShared.sensorMask  = mask;
    commsShared.hasImu      = hasImu;
    commsShared.yaw   = yaw;
    commsShared.pitch = pitch;
    commsShared.roll  = roll;
    commsShared.dataTimestamp = millis();
    memcpy(commsShared.sensorReadings, readings, count * sizeof(int16_t));
    mutex_exit(&commsSharedMutex);
}

bool commsPollMotorCmd(MotorState out[NUM_MOTORS]) {
    mutex_enter_blocking(&commsSharedMutex);
    bool pending = commsShared.motorCmdPending;
    if (pending) {
        memcpy(out, commsShared.motorCmd, sizeof(MotorState) * NUM_MOTORS);
        commsShared.motorCmdPending = false;
    }
    mutex_exit(&commsSharedMutex);
    return pending;
}

RobotState commsGetState() {
    return commsShared.state;  // volatile read
}

void commsSetState(RobotState s) {
    mutex_enter_blocking(&commsSharedMutex);
    commsShared.state = s;
    mutex_exit(&commsSharedMutex);
}

// -- Core 1 entry points -----------------------------------------------

void commsSetup() {
    mutex_init(&commsSharedMutex);

    memset(&commsShared, 0, sizeof(commsShared));
    commsShared.state          = STATE_IDLE;
    commsShared.sendIntervalMs = SEND_INTERVAL_MS;

    serverIP.fromString(SERVER_IP);

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    // Block core 1 until WiFi connects (core 0 can proceed with HW init)
    while (WiFi.status() != WL_CONNECTED) {
        delay(250);
    }

    udp.begin(0);  // ephemeral port -- server learns it from first packet

    // Send first alive immediately
    sendAlive();
    lastAliveSent = millis();
}

void commsLoop() {
    // Always check for inbound commands
    handleRx();

    RobotState st = commsGetState();
    uint32_t now  = millis();

    switch (st) {
        case STATE_IDLE:
            // Heartbeat every ALIVE_INTERVAL_MS
            if (now - lastAliveSent >= ALIVE_INTERVAL_MS) {
                sendAlive();
                lastAliveSent = now;
            }
            break;

        case STATE_SETUP_REQ:
            // Waiting for core 0 to finish sensor init -- nothing to send
            break;

        case STATE_SETUP_DONE:
            // Send device info once, then wait for CMD_START_LOG
            if (!deviceInfoSent) {
                sendDeviceInfo();
                deviceInfoSent = true;
            }
            break;

        case STATE_LOGGING: {
            uint16_t interval;
            mutex_enter_blocking(&commsSharedMutex);
            interval = commsShared.sendIntervalMs;
            mutex_exit(&commsSharedMutex);

            if (now - lastDataSent >= interval) {
                sendSensorData();
                lastDataSent = now;
            }
            break;
        }
    }
}
