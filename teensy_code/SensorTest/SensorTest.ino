#include <Wire.h>
#include <VL6180X.h>

VL6180X sensor1;
VL6180X sensor2;
VL6180X sensor3;
VL6180X sensor4;
int count = 0;

// Sensor pins
const int sensor1_pin = 9;
const int sensor2_pin = 10;
// We arbitrarily chose pins 11 and 12 for the other two sensors
const int sensor3_pin = 11;
const int sensor4_pin = 12;

#define LED_PIN LED_BUILTIN

void init_sensors() {
  pinMode(sensor1_pin, OUTPUT);
  pinMode(sensor2_pin, OUTPUT);
  pinMode(sensor3_pin, OUTPUT);
  pinMode(sensor4_pin, OUTPUT);
  digitalWrite(sensor1_pin, LOW);
  digitalWrite(sensor2_pin, LOW);
  digitalWrite(sensor3_pin, LOW);
  digitalWrite(sensor4_pin, LOW);

  Wire.begin();
  digitalWrite(sensor1_pin, HIGH);
  delay(50);
  sensor1.init();
  sensor1.configureDefault();
  sensor1.setTimeout(500);
  sensor1.setAddress(0x54);
  

  digitalWrite(sensor2_pin, HIGH);
  delay(50);
  sensor2.init();
  sensor2.configureDefault();
  sensor2.setTimeout(500);
  sensor2.setAddress(0x56);

  digitalWrite(sensor3_pin, HIGH);
  delay(50);
  sensor3.init();
  sensor3.configureDefault();
  sensor3.setTimeout(500);
  sensor3.setAddress(0x58);

  digitalWrite(sensor4_pin, HIGH);
  delay(50);
  sensor4.init();
  sensor4.configureDefault();
  sensor4.setTimeout(500);
  sensor4.setAddress(0x60);
}


void setup() {
  // put your setup code here, to run once:
  Serial.begin(9600);
  pinMode(LED_PIN, OUTPUT);
  init_sensors();
  digitalWrite(LED_PIN, HIGH);
}

void loop() {
  // put your main code here, to run repeatedly:
  Serial.print("HELLO");
  Serial.println(count);
  const uint16_t sensor1Value = sensor1.readRangeSingleMillimeters();
  const uint16_t sensor2Value = sensor2.readRangeSingleMillimeters();
  const uint16_t sensor3Value = sensor3.readRangeSingleMillimeters();
  const uint16_t sensor4Value = sensor4.readRangeSingleMillimeters();
  Serial.print("Sensor1: ");
  Serial.println(sensor1Value);
  Serial.print("Sensor2: ");
  Serial.println(sensor2Value);
  Serial.print("Sensor3: ");
  Serial.println(sensor3Value);
  Serial.print("Sensor4: ");
  Serial.println(sensor4Value);
  count++;
}
