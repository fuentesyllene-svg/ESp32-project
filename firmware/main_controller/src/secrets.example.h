#pragma once
// Copy to secrets.h and fill in. secrets.h is gitignored - never commit
// credentials to the repository.

#define WIFI_SSID       "your-field-hotspot"
#define WIFI_PASSWORD   "your-password"

// MQTT broker for the cloud IoT dashboard (HiveMQ, Mosquitto, Adafruit IO,
// ThingsBoard...). Leave MQTT_USER empty for an anonymous broker.
#define MQTT_HOST       "broker.example.com"
#define MQTT_PORT       1883
#define MQTT_USER       ""
#define MQTT_PASSWORD   ""
