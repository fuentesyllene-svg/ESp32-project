#pragma once

// Entry points for the sketch. The .ino stays minimal and simply forwards to
// these, because the Arduino IDE's automatic prototype generator inserts
// declarations at file scope and mis-handles the functions this project keeps
// inside namespaces.
namespace app {
void setup();
void loop();
}  // namespace app
