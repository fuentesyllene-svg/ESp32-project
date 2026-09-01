#pragma once
#include <Arduino.h>

// Local status page, served both on the field Wi-Fi and on the fallback
// hotspot. This is the "local operational status log sent to the user" path:
// it keeps working when the cloud link is down.
namespace webui {
using StatusProvider = String (*)();
using CommandHandler = void (*)(const String& json_payload);

void begin(StatusProvider status, CommandHandler command);
void loop();
}  // namespace webui
