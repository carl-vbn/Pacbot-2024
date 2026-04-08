#include "sensors.h"

// -- State -------------------------------------------------------------
static VL6180X         tofSensors[MAX_SENSORS];
static Adafruit_BNO055 bno(55, BNO055_ADDRESS, &Wire);

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

    digitalWrite(CE_PINS[index], HIGH);
    delay(15);

    if (!i2cProbe(Wire1, DEFAULT_ADDR)) {
        digitalWrite(CE_PINS[index], LOW);
        return false;
    }

    tofSensors[index].setBus(&Wire1);
    tofSensors[index].setTimeout(300);
    tofSensors[index].init();

    if (!i2cProbe(Wire1, DEFAULT_ADDR)) {
        digitalWrite(CE_PINS[index], LOW);
        return false;
    }

    tofSensors[index].setAddress(ADDRESSES[index]);

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

    bno = Adafruit_BNO055(55, BNO055_ADDRESS, &Wire);

    if (!bno.begin()) return false;

    bno.setExtCrystalUse(true);
    return true;
}

// -- Public API --------------------------------------------------------

void sensorsHardwareInit() {
    for (uint8_t i = 0; i < MAX_SENSORS; i++) {
        pinMode(CE_PINS[i], OUTPUT);
        digitalWrite(CE_PINS[i], LOW);
        sensorPresent[i] = false;
    }

    Wire1.setSDA(TOF_SDA_PIN);
    Wire1.setSCL(TOF_SCL_PIN);
    Wire1.begin();

    Wire.setSDA(IMU_SDA_PIN);
    Wire.setSCL(IMU_SCL_PIN);
    Wire.begin();
}

uint8_t sensorsInit() {
    numSensorsPresent = 0;

    for (uint8_t i = 0; i < MAX_SENSORS; i++) {
        sensorPresent[i] = initialiseSensor(i);
        if (sensorPresent[i]) numSensorsPresent++;
    }

    imuPresent = initialiseIMU();

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

    imu::Vector<3> euler = bno.getVector(Adafruit_BNO055::VECTOR_EULER);
    yaw   = euler.x();
    pitch = euler.y();
    roll  = euler.z();
    return true;
}
