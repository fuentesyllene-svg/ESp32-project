#pragma once

// Entry points for the sketch. See the note in the controller's app.h: the
// .ino is kept minimal because the Arduino IDE's prototype generator does not
// handle functions declared inside namespaces.
namespace app {
void setup();
void loop();
}  // namespace app
