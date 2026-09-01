/*
  IoT-Enabled Caterpillar and Aphid Detection and Removal System
  Solar-Powered UV-Light and Fan Trap for Lactuca sativa

  MAIN CONTROLLER  -  ESP32 DevKit v1

  Arduino IDE setup
  -----------------
  Board:            "ESP32 Dev Module"   (Tools > Board > esp32)
  Partition Scheme: "Default 4MB with spiffs (1.2MB APP/1.5MB SPIFFS)"
  Upload Speed:     921600
  Libraries (Tools > Manage Libraries):
      PubSubClient              by Nick O'Leary
      ArduinoJson               by Benoit Blanchon   (version 7.x)
      DHT sensor library        by Adafruit
      Adafruit Unified Sensor   by Adafruit

  Before uploading, open the secrets.h tab and fill in your Wi-Fi and MQTT
  details. Wiring is in docs/WIRING.md - fit the blower's flyback diode and
  bulk capacitor before powering the fan.

  The other tabs hold the implementation; this file only starts it.
*/

#include "app.h"

void setup() {
  app::setup();
}

void loop() {
  app::loop();
}
