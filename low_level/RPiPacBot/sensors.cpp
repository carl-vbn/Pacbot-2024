#include "sensors.h"

// -- State -------------------------------------------------------------
static VL6180X          tofSensors[MAX_SENSORS];
static Adafruit_BNO055 *bno = nullptr;

bool    sensorPresent[MAX_SENSORS];
uint8_t numSensorsPresent = 0;
bool    imuPresent        = false;

// -- Internal helpers --------------------------------------------------

static bool i2cProbe(TwoWire &bus, uint8_t address) {
    bus.beginTransmission(address);
    return (bus.endTransmission() == 0);
}

static bool initialiseSensor(uint8_t index) {
    const uint8_t DEFAULT_ADDR = 0x29;

    // Ensure CE was LOW long enough for a clean reset, then bring up.
    digitalWrite(CE_PINS[index], LOW);
    delay(10);
    digitalWrite(CE_PINS[index], HIGH);
    delay(100);

    // After CE toggle the sensor should be at 0x29.  But if a previous
    // firmware run already assigned it ADDRESSES[index] and the reset
    // didn't fully take, it may still respond there.  Check both.
    bool atDefault = i2cProbe(Wire1, DEFAULT_ADDR);
    bool atTarget  = i2cProbe(Wire1, ADDRESSES[index]);

    if (!atDefault && !atTarget) {
        digitalWrite(CE_PINS[index], LOW);
        return false;
    }

    tofSensors[index].setBus(&Wire1);
    tofSensors[index].setTimeout(300);

    if (atDefault) {
        // Normal path: sensor booted fresh at 0x29
        tofSensors[index].init();
        tofSensors[index].setAddress(ADDRESSES[index]);
    } else {
        // Sensor survived at its old address — re-init in place
        tofSensors[index].setAddress(ADDRESSES[index]);
        tofSensors[index].init();
    }

    if (!i2cProbe(Wire1, ADDRESSES[index])) {
        digitalWrite(CE_PINS[index], LOW);
        return false;
    }

    tofSensors[index].configureDefault();
    tofSensors[index].stopContinuous();
    return true;
}

static bool initialiseIMU() {
    if (!i2cProbe(Wire, BNO055_ADDRESS)) return false;

    bno = new Adafruit_BNO055(55, BNO055_ADDRESS, &Wire);

    if (!bno->begin()) return false;

    bno->setExtCrystalUse(true);
    return true;
}

// -- Public API --------------------------------------------------------

// Bit-bang up to 9 SCL clocks to free a stuck SDA line.
// Must be called BEFORE Wire.begin() while the pins are still GPIO.
static void i2cBusRecover(uint8_t sdaPin, uint8_t sclPin) {
    pinMode(sdaPin, INPUT_PULLUP);
    pinMode(sclPin, OUTPUT);

    for (uint8_t i = 0; i < 9; i++) {
        if (digitalRead(sdaPin) == HIGH) break;   // bus is free
        digitalWrite(sclPin, LOW);
        delayMicroseconds(5);
        digitalWrite(sclPin, HIGH);
        delayMicroseconds(5);
    }
}

void sensorsHardwareInit() {
    for (uint8_t i = 0; i < MAX_SENSORS; i++) {
        pinMode(CE_PINS[i], OUTPUT);
        digitalWrite(CE_PINS[i], LOW);
        sensorPresent[i] = false;
    }

    // Hold CE LOW long enough for a full power-on reset (~400 us per
    // datasheet, but sensors that were already awake at stale addresses
    // from a previous firmware run need a longer hold).
    delay(100);

    // If a previous upload interrupted mid-I2C-transaction, SDA may be
    // stuck LOW.  Clock-cycle recovery frees it before the Wire library
    // takes ownership of the pins.
    i2cBusRecover(TOF_SDA_PIN, TOF_SCL_PIN);

    Wire1.setSDA(TOF_SDA_PIN);
    Wire1.setSCL(TOF_SCL_PIN);
    Wire1.begin();

    i2cBusRecover(IMU_SDA_PIN, IMU_SCL_PIN);

    Wire.setSDA(IMU_SDA_PIN);
    Wire.setSCL(IMU_SCL_PIN);
    Wire.begin();
}

uint8_t sensorsInit() {
    numSensorsPresent = 0;

    for (uint8_t i = 0; i < MAX_SENSORS; i++) {
        sensorPresent[i] = initialiseSensor(i);
        if (sensorPresent[i]) numSensorsPresent++;
        delay(100);
    }

    imuPresent = initialiseIMU();
    // imuPresent = false;

    return numSensorsPresent;
}

int16_t sensorReadMM(uint8_t index) {
    if (index >= MAX_SENSORS || !sensorPresent[index]) return -1;

    uint8_t range = tofSensors[index].readRangeSingleMillimeters();

    if (tofSensors[index].timeoutOccurred()) return -1;
    return (int16_t)range;
}

bool imuReadEuler(float &yaw, float &pitch, float &roll) {
    if (!imuPresent) return false;

    imu::Vector<3> euler = bno->getVector(Adafruit_BNO055::VECTOR_EULER);
    yaw   = euler.x();
    pitch = euler.y();
    roll  = euler.z();
    return true;
}
