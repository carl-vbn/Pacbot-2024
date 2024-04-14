#include <Wire.h>
#include <VL6180X.h>

// Motor pins
const unsigned int MOTORDIR_PINS[4] = {5, 7, 23, 15};
const unsigned int MOTORPWM_PINS[4] = {6, 8, 22, 14};
const unsigned int gpio_pin_1 = 2; 
const unsigned int gpio_pin_2 = 3; 
const unsigned int gpio_pin_3 = 4; 
const unsigned int X = 10;
VL6180X sensor1;
VL6180X sensor2;
VL6180X sensor3;
VL6180X sensor4;

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

void init_motors() {
  for (int i = 0; i<4; i++) {
    pinMode(MOTORDIR_PINS[i], OUTPUT);
    pinMode(MOTORPWM_PINS[i], OUTPUT);

    digitalWrite(MOTORDIR_PINS[i], LOW);
    analogWrite(MOTORPWM_PINS[i], 0);
  }
}

void setup()
{
  pinMode(LED_PIN, OUTPUT);
  pinMode(gpio_pin_1, INPUT);
  pinMode(gpio_pin_2, INPUT);
  pinMode(gpio_pin_3, INPUT);
  init_sensors();
  init_motors();
  digitalWrite(LED_PIN, HIGH);
}

void loop()
{
  const int sensor1Value = sensor1.readRangeSingleMillimeters();
  const int sensor2Value = sensor2.readRangeSingleMillimeters();
  const int sensor3Value = sensor3.readRangeSingleMillimeters();
  const int sensor4Value = sensor4.readRangeSingleMillimeters();

  // 3 bits - 8 possibilities 
  // bit1 bit2 bit3
  // 000 not move (0)
  // 001 left (1)
  // 010 right (2)
  // 011 forward (3)
  // 100 back (4)
  gpio1_val = digitalRead(gpio_pin_1); 
  gpio2_val = digitalRead(gpio_pin_2);
  gpio3_val = digitalRead(gpio_pin_3);
  bot_direction = 4*gpio1_val + 2*gpio2_val + 1*gpio3_val;

  int top_sensor;
  int bottom_sensor;
  int left_sensor;
  int right_sensor;

  // indices of motors
  int top_motor;
  int bottom_motor;
  int left_motor;
  int right_motor;
  
  if (bot_direction == 0) {  //not moving
    for (int i = 0; i<4; i++) {
      digitalWrite(MOTORDIR_PINS[i], LOW); 
      analogWrite(MOTORPWM_PINS[i], 0);
    }
  } else if (bot_direction == 1) {  //going left
      top_sensor = sensor4;
      bottom_sensor = sensor2;
      left_sensor = sensor3;
      right_sensor = sensor1;

      top_motor = 3;
      bottom_motor = 1;
      left_motor = 2;
      right_motor = 0;
    // sensor 2 is at the top, sensor 1 and 3 are sides, sensor 4 is that back
  } else if (bot_direction == 2) { // going right
      top_sensor = sensor2;
      bottom_sensor = sensor4;
      left_sensor = sensor1;
      right_sensor = sensor3;

      top_motor = 1;
      bottom_motor = 3;
      left_motor = 0;
      right_motor = 2;
    //sensor 4 is at the top, sensor 1 and 3 is at the sides, sensor 2 is at the back
  } else if (bot_direction == 3) { //move forward
      top_sensor = sensor1;
      bottom_sensor = sensor3;
      left_sensor = sensor4;
      right_sensor = sensor2;

      top_motor = 0;
      bottom_motor = 2;
      left_motor = 3;
      right_motor = 1;
    // sensor1 is top, sensor 2 and 4 is at the sides, sensor 3 is at the bottom
  } else { // move backward
      top_sensor = sensor3;
      bottom_sensor = sensor1;
      left_sensor = sensor2;
      right_sensor = sensor4;

      top_motor = 2;
      bottom_motor = 0;
      left_motor = 1;
      right_motor = 3;
  }
  // IF THE top sensor reading is less than X (value to be determined later), don't go straight
  // OR 
  go_straight(top_motor, bottom_motor, left_motor, right_motor, left_sensor, right_sensor);

// for (int i = 0; i<4; i++) {
//   digitalWrite(MOTORDIR_PINS[i], LOW); // direction of motor
//   analogWrite(MOTORPWM_PINS[i], 200); // speed of motor - 0 to 255 speed

  //   delay(1000);

  //   digitalWrite(MOTORDIR_PINS[i], HIGH); // Reverse 

  //   delay(1000);

  //   analogWrite(MOTORPWM_PINS[i], 0);

  //   delay(500);
  // }
}