/*
    Raspberry Pi Pico 2 W + up to 8x Pololu VL6180X ToF sensors
    All sensors share the same I2C bus. CE pins are used at startup
    to bring each sensor up one at a time and assign it a unique address.
    Sensors that fail to initialise are marked absent and skipped.

    Wiring (all sensors):
      VIN -> 3.3 V
      GND -> GND
      SDA -> GP18  (I2C1 SDA)
      SCL -> GP19  (I2C1 SCL)
      CE  -> see CE_PINS below

    Library: VL6180X by Pololu (install via Library Manager)
*/

#include <Wire.h>
#include <VL6180X.h>

// -- Configuration -----------------------------------------------------
#define SDA_PIN          18
#define SCL_PIN          19
#define READ_INTERVAL_MS 100   // 10 Hz
#define MAX_SENSORS      8

// CE pin and assigned I2C address for each candidate sensor slot.
// All VL6180X boot at 0x29 -- we reassign them one by one at startup.
// Addresses 0x28-0x2F are safely outside the default 0x29 so there
// is no aliasing risk during the sequential bring-up.
const uint8_t CE_PINS[MAX_SENSORS]   = { 17,   2,    13,   0,    1,    3,    4,    5   };
const uint8_t ADDRESSES[MAX_SENSORS] = { 0x28, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F, 0x30 };

// -- State -------------------------------------------------------------
VL6180X sensors[MAX_SENSORS];
bool    sensorPresent[MAX_SENSORS];   // true -> init succeeded
uint8_t numPresent = 0;

// -- Helpers -----------------------------------------------------------

/*
 * Block until a specific character is received on Serial.
 * Discards all other incoming bytes while waiting.
 */
void waitForChar(char target) {
    while (true) {
        if (Serial.available() > 0) {
            char c = (char)Serial.read();
            if (c == target) return;
        }
    }
}

/*
 * Probe whether anything ACKs on the I2C bus at the given address.
 * Returns true if an ACK is received (device present).
 */
bool i2cProbe(uint8_t address) {
    Wire1.beginTransmission(address);
    return (Wire1.endTransmission() == 0);
}

/*
 * Initialise one sensor slot:
 *   1. Assert CE HIGH  -> device boots at default address 0x29
 *   2. Probe 0x29      -> confirms device is alive on the bus
 *   3. init()          -> reads/writes the device register map
 *   4. setAddress()    -> moves it to its permanent slot address
 *   5. configureDefault / stopContinuous
 *
 * Returns true on success, false if the device was absent or unresponsive.
 */
bool initialiseSensor(uint8_t index) {
    const uint8_t DEFAULT_ADDR = 0x29;

    // Release from reset -- device will boot at 0x29
    digitalWrite(CE_PINS[index], HIGH);
    delay(100);  // datasheet: >= 400 us after power-on; 15 ms is comfortable

    // Quick I2C probe before committing to a full init
    if (!i2cProbe(DEFAULT_ADDR)) {
        digitalWrite(CE_PINS[index], LOW);  // put absent device back in reset
        return false;
    }

    sensors[index].setBus(&Wire1);
    sensors[index].setTimeout(300);
    sensors[index].init();

    // Verify init succeeded by probing the *default* address one more time
    // (init() itself does not return a status code in this library)
    if (!i2cProbe(DEFAULT_ADDR)) {
        digitalWrite(CE_PINS[index], LOW);
        return false;
    }

    sensors[index].setAddress(ADDRESSES[index]);

    // Confirm the device now responds at its new address
    if (!i2cProbe(ADDRESSES[index])) {
        digitalWrite(CE_PINS[index], LOW);
        return false;
    }

    sensors[index].configureDefault();
    sensors[index].stopContinuous();
    return true;
}

/*
 * Read the single-shot range from a present sensor (mm).
 * Returns -1 on timeout or if the sensor is absent.
 */
int16_t readSensorDistanceMM(uint8_t index) {
    if (index >= MAX_SENSORS || !sensorPresent[index]) return -1;

    uint8_t range = sensors[index].readRangeSingleMillimeters();

    if (sensors[index].timeoutOccurred()) return -1;
    return (int16_t)range;
}

// -- Setup -------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    {
        unsigned long t0 = millis();
        while (!Serial && millis() - t0 < 3000);
    }

    Serial.println("=== Pico 2 W + up to 8x Pololu VL6180X ===");
    Serial.println();

    // Hold ALL sensors in reset before touching the bus.
    // Do this before waiting for 'S' so the bus is clean regardless
    // of how long the user takes to send the start command.
    for (uint8_t i = 0; i < MAX_SENSORS; i++) {
        pinMode(CE_PINS[i], OUTPUT);
        digitalWrite(CE_PINS[i], LOW);
        sensorPresent[i] = false;
    }

    Wire1.setSDA(SDA_PIN);
    Wire1.setSCL(SCL_PIN);
    Wire1.begin();

    // -- Wait for start command --
    Serial.println("Send 'S' to begin sensor initialisation...");
    waitForChar('S');
    Serial.println("'S' received. Starting setup.");
    Serial.println();

    // -- Sequential sensor bring-up --
    Serial.println("Scanning for sensors...");
    Serial.println();

    for (uint8_t i = 0; i < MAX_SENSORS; i++) {
        Serial.print("  Slot ");
        Serial.print(i);
        Serial.print("  CE->GP");
        Serial.print(CE_PINS[i]);
        Serial.print("  addr->0x");
        if (ADDRESSES[i] < 0x10) Serial.print('0');
        Serial.print(ADDRESSES[i], HEX);
        Serial.print(" ... ");

        sensorPresent[i] = initialiseSensor(i);

        if (sensorPresent[i]) {
            numPresent++;
            Serial.println("FOUND");
        } else {
            Serial.println("not found");
        }

        delay(100);
    }

    // -- Summary --
    Serial.println();
    Serial.println("------------------------------------------");
    Serial.print("Sensors found: ");
    Serial.print(numPresent);
    Serial.print(" / ");
    Serial.println(MAX_SENSORS);
    Serial.println();

    Serial.print("  Present  on pins: ");
    bool first = true;
    for (uint8_t i = 0; i < MAX_SENSORS; i++) {
        if (sensorPresent[i]) {
            if (!first) Serial.print(", ");
            Serial.print("GP");
            Serial.print(CE_PINS[i]);
            first = false;
        }
    }
    if (first) Serial.print("(none)");
    Serial.println();

    Serial.print("  Absent   on pins: ");
    first = true;
    for (uint8_t i = 0; i < MAX_SENSORS; i++) {
        if (!sensorPresent[i]) {
            if (!first) Serial.print(", ");
            Serial.print("GP");
            Serial.print(CE_PINS[i]);
            first = false;
        }
    }
    if (first) Serial.print("(none)");
    Serial.println();
    Serial.println("------------------------------------------");
    Serial.println();

    // -- Wait for logging command --
    Serial.println("Send 'L' to begin continuous sensor logging...");
    waitForChar('L');
    Serial.println("'L' received. Starting logging.");
    Serial.println();

    // Print column headers for present sensors only
    first = true;
    for (uint8_t i = 0; i < MAX_SENSORS; i++) {
        if (sensorPresent[i]) {
            if (!first) Serial.print(", ");
            Serial.print("GP");
            Serial.print(CE_PINS[i]);
            Serial.print("(mm)");
            first = false;
        }
    }
    Serial.println();
}

// -- Main loop ---------------------------------------------------------
void loop() {
    static uint32_t lastRead = 0;

    if (millis() - lastRead < READ_INTERVAL_MS) return;
    lastRead = millis();

    bool first = true;
    for (uint8_t i = 0; i < MAX_SENSORS; i++) {
        if (!sensorPresent[i]) continue;

        if (!first) Serial.print(", ");

        int16_t dist = readSensorDistanceMM(i);
        if (dist < 0) {
            Serial.print("ERR");
        } else {
            Serial.print(dist);
        }

        first = false;
    }
    Serial.println();
}