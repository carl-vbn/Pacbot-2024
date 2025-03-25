#include "sensors.h"
#include "motors.h"
#include "pacbot_state.h"

const unsigned int gpio_pin_1 = 2; 
const unsigned int gpio_pin_2 = 3; 
const unsigned int gpio_pin_3 = 4;

void setup()
{
  Serial.begin(9600);
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(gpio_pin_1, INPUT);
  pinMode(gpio_pin_2, INPUT);
  pinMode(gpio_pin_3, INPUT);
  init_sensors();
  init_motors();

  init_state();

  digitalWrite(LED_BUILTIN, HIGH);

  Serial.println("Setup complete");
}

long last_tick_time = 0;
void loop() {
  long now = millis();
  movement_tick(now - last_tick_time);
  delay(10);
  last_tick_time = now;
}

