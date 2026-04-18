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
static uint32_t      lastWiFiCheck  = 0;
static bool          wifiWasConnected = false;

#define WIFI_CHECK_INTERVAL_MS 2000

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

// -- Forward declarations ----------------------------------------------
static void sendDeviceInfo();

// -- Inbound command handling ------------------------------------------

static void handleRx() {
    int packetSize = udp.parsePacket();
    while (packetSize > 0) {
        int n = udp.read(rxBuf, sizeof(rxBuf));
        if (n < 1) { packetSize = udp.parsePacket(); continue; }

        uint8_t type = rxBuf[0];

        switch (type) {
            case CMD_START_LOG: {
                RobotState cur = commsGetState();
                if (cur == STATE_IDLE) {
                    commsSetState(STATE_LOGGING);
                }
                break;
            }

            case CMD_STOP_LOG: {
                RobotState cur = commsGetState();
                if (cur == STATE_LOGGING) {
                    commsSetState(STATE_IDLE);
                }
                break;
            }

            case CMD_SET_MOTOR: {
                RobotState cur = commsGetState();
                if (n >= 4 && (cur == STATE_IDLE || cur == STATE_LOGGING)) {
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
                if (n >= 1 + NUM_MOTORS * 2 && (cur == STATE_IDLE || cur == STATE_LOGGING)) {
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

            case CMD_STATUS: {
                sendDeviceInfo();
                break;
            }

            case CMD_SET_DRIVE_MODE: {
                if (n >= 2) {
                    uint8_t m = rxBuf[1];
                    if (m <= DRIVE_CARDINAL_LOCKED) {
                        mutex_enter_blocking(&commsSharedMutex);
                        commsShared.pendingDriveMode    = (DriveMode)m;
                        commsShared.driveModeCmdPending = true;
                        mutex_exit(&commsSharedMutex);
                    }
                }
                break;
            }

            case CMD_CARDINAL_MOVE: {
                RobotState cur = commsGetState();
                if (n >= 3 && (cur == STATE_IDLE || cur == STATE_LOGGING)) {
                    uint8_t dir   = rxBuf[1];
                    uint8_t speed = rxBuf[2];
                    if (dir <= DIR_WEST) {
                        mutex_enter_blocking(&commsSharedMutex);
                        commsShared.pendingCardinalDir   = (CardinalDir)dir;
                        commsShared.pendingCardinalSpeed = speed;
                        commsShared.cardinalCmdPending   = true;
                        mutex_exit(&commsSharedMutex);
                    }
                }
                break;
            }

            case CMD_CALIBRATE: {
                mutex_enter_blocking(&commsSharedMutex);
                commsShared.calibratePending = true;
                mutex_exit(&commsSharedMutex);
                break;
            }

            case CMD_SET_PID: {
                if (n >= 14) {
                    uint8_t loop = rxBuf[1];
                    float kp, ki, kd;
                    memcpy(&kp, rxBuf + 2, 4);
                    memcpy(&ki, rxBuf + 6, 4);
                    memcpy(&kd, rxBuf + 10, 4);
                    if (loop <= PID_LOOP_FORWARD) {
                        mutex_enter_blocking(&commsSharedMutex);
                        commsShared.pendingPidLoop = loop;
                        commsShared.pendingPidKp   = kp;
                        commsShared.pendingPidKi   = ki;
                        commsShared.pendingPidKd   = kd;
                        commsShared.pidGainsPending = true;
                        mutex_exit(&commsSharedMutex);
                    }
                }
                break;
            }

            case CMD_SET_SENSOR_OFFSETS: {
                if (n >= 1 + MAX_SENSORS * 2) {
                    mutex_enter_blocking(&commsSharedMutex);
                    for (uint8_t i = 0; i < MAX_SENSORS; i++) {
                        int16_t off;
                        memcpy(&off, rxBuf + 1 + i * 2, 2);
                        commsShared.pendingSensorOffsets[i] = off;
                    }
                    commsShared.sensorOffsetsPending = true;
                    mutex_exit(&commsSharedMutex);
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

void commsSetDeviceInfo(uint8_t count, uint8_t mask, bool hasImu) {
    mutex_enter_blocking(&commsSharedMutex);
    commsShared.sensorCount    = count;
    commsShared.sensorMask     = mask;
    commsShared.hasImu         = hasImu;
    commsShared.deviceInfoReady = true;
    mutex_exit(&commsSharedMutex);
}

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
    if (readings && count > 0)
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

bool commsPollDriveMode(DriveMode &mode) {
    mutex_enter_blocking(&commsSharedMutex);
    bool pending = commsShared.driveModeCmdPending;
    if (pending) {
        mode = commsShared.pendingDriveMode;
        commsShared.driveModeCmdPending = false;
    }
    mutex_exit(&commsSharedMutex);
    return pending;
}

bool commsPollCardinalCmd(CardinalDir &dir, uint8_t &speed) {
    mutex_enter_blocking(&commsSharedMutex);
    bool pending = commsShared.cardinalCmdPending;
    if (pending) {
        dir   = commsShared.pendingCardinalDir;
        speed = commsShared.pendingCardinalSpeed;
        commsShared.cardinalCmdPending = false;
    }
    mutex_exit(&commsSharedMutex);
    return pending;
}

bool commsPollCalibrate() {
    mutex_enter_blocking(&commsSharedMutex);
    bool pending = commsShared.calibratePending;
    if (pending) commsShared.calibratePending = false;
    mutex_exit(&commsSharedMutex);
    return pending;
}

bool commsPollPidGains(uint8_t &loop, float &kp, float &ki, float &kd) {
    mutex_enter_blocking(&commsSharedMutex);
    bool pending = commsShared.pidGainsPending;
    if (pending) {
        loop = commsShared.pendingPidLoop;
        kp   = commsShared.pendingPidKp;
        ki   = commsShared.pendingPidKi;
        kd   = commsShared.pendingPidKd;
        commsShared.pidGainsPending = false;
    }
    mutex_exit(&commsSharedMutex);
    return pending;
}

bool commsPollSensorOffsets(int16_t offsets[MAX_SENSORS]) {
    mutex_enter_blocking(&commsSharedMutex);
    bool pending = commsShared.sensorOffsetsPending;
    if (pending) {
        memcpy(offsets, commsShared.pendingSensorOffsets,
               sizeof(int16_t) * MAX_SENSORS);
        commsShared.sensorOffsetsPending = false;
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

void commsSharedInit() {
    mutex_init(&commsSharedMutex);
    memset(&commsShared, 0, sizeof(commsShared));
    commsShared.state          = STATE_IDLE;
    commsShared.sendIntervalMs = SEND_INTERVAL_MS;
}

void commsSetup() {
    serverIP.fromString(SERVER_IP);

    WiFi.mode(WIFI_STA);
    if (strlen(WIFI_PASSWORD) == 0) {
        WiFi.begin(WIFI_SSID);
    } else {
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    }

    // Block core 1 until WiFi connects (core 0 can proceed with HW init)
    while (WiFi.status() != WL_CONNECTED) {
        delay(250);
    }

    udp.begin(0);  // ephemeral port -- server learns it from first packet
    wifiWasConnected = true;

    // Send first alive immediately (device info is sent once core 0 signals ready)
    sendAlive();
    lastAliveSent = millis();
    lastWiFiCheck = lastAliveSent;
}

void commsLoop() {
    uint32_t now = millis();

    // Periodic WiFi connection check / reconnect
    if (now - lastWiFiCheck >= WIFI_CHECK_INTERVAL_MS) {
        lastWiFiCheck = now;
        bool connected = (WiFi.status() == WL_CONNECTED);
        if (!connected) {
            if (wifiWasConnected) {
                udp.stop();
                wifiWasConnected = false;
            }
            // Kick off a (re)connect attempt; non-blocking
            if (strlen(WIFI_PASSWORD) == 0) {
                WiFi.begin(WIFI_SSID);
            } else {
                WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
            }
        } else if (!wifiWasConnected) {
            // Just (re)connected -- reopen socket and resend device info
            udp.begin(0);
            wifiWasConnected = true;
            deviceInfoSent   = false;
            sendAlive();
            lastAliveSent = now;
        }
    }

    // Skip all send/receive work while disconnected
    if (WiFi.status() != WL_CONNECTED) return;

    // Always check for inbound commands
    handleRx();

    RobotState st = commsGetState();

    switch (st) {
        case STATE_IDLE: {
            // Send device info once core 0 has populated it
            if (!deviceInfoSent) {
                bool ready;
                mutex_enter_blocking(&commsSharedMutex);
                ready = commsShared.deviceInfoReady;
                mutex_exit(&commsSharedMutex);
                if (ready) {
                    sendDeviceInfo();
                    deviceInfoSent = true;
                }
            }

            if (now - lastAliveSent >= ALIVE_INTERVAL_MS) {
                sendAlive();
                lastAliveSent = now;
            }
            break;
        }

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
