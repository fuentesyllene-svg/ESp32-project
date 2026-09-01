/*
  IoT-Enabled Caterpillar and Aphid Detection and Removal System
  Solar-Powered UV-Light and Fan Trap for Lactuca sativa

  DETECTION NODE  -  ESP32-CAM (AI-Thinker, OV2640)

  Arduino IDE setup
  -----------------
  Board:            "AI Thinker ESP32-CAM"   (Tools > Board > esp32)
  Partition Scheme: "Huge APP (3MB No OTA/1MB SPIFFS)"     <- required
  PSRAM:            Enabled
  Upload Speed:     460800

  No extra libraries are needed: the camera driver ships with the ESP32 board
  package.

  Uploading
  ---------
  1. Disconnect the two link wires on GPIO13 and GPIO14 first.
  2. Connect an FTDI/USB-serial adapter: 5V, GND, U0T -> RX, U0R -> TX.
  3. Jumper GPIO0 to GND, press RESET, then upload.
  4. Remove the GPIO0 jumper, press RESET, and reconnect the link wires.

  This board only looks and reports. All actuation and networking live on the
  main controller; the two talk over the UART protocol in docs/LINK_PROTOCOL.md.
*/

#include "app.h"

void setup() {
  app::setup();
}

void loop() {
  app::loop();
}
