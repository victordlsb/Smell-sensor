# WC Sensor

A WiFi-enabled bathroom odor detector built with an ESP8266 (NodeMCU) and an MQ135 gas sensor.

The device monitors air quality continuously and sends configurable HTTP notifications whenever the measured value crosses a configurable threshold. It also exposes a built-in web interface for configuration and monitoring, eliminating the need to recompile the firmware for most changes.

---

# Features

- WiFi configuration through captive portal (WiFiManager)
- Built-in web configuration interface
- Configurable odor threshold
- Configurable webhook endpoint (HTTP or HTTPS)
- Custom notification messages
- Dynamic variable substitution (`[value]`)
- Test notification using the onboard FLASH button
- Configuration stored permanently in flash (LittleFS)
- JSON status endpoint
- Automatic hostname via mDNS (`wc-sensor.local`)

---

# Hardware

- ESP8266 NodeMCU V3 (ESP-12F)
- MQ135 Gas Sensor
- USB power supply (recommended)

---

# Wiring

## MQ135

| MQ135 | ESP8266 |
|--------|----------|
| VCC | 3V3 *(or VIN depending on your module)* |
| GND | GND |
| AO | A0 |

The onboard FLASH button is used for testing and configuration reset.

---

# How it works

The sensor continuously reads the analog output from the MQ135.

The firmware keeps track of the current state:

- Air is OK
- Bad smell detected

A notification is only sent when the value crosses the configured threshold.

Example:

```
Current value: 320

Threshold: 350

320 -> 330 -> 345 -> 360
                   ↑
        Notification sent
```

The device remembers its current state, preventing repeated notifications while the value remains above (or below) the threshold.

---

# Notification messages

Three independent messages can be configured:

- Bad smell
- Air back to normal
- Test message

The placeholder

```
[value]
```

is automatically replaced with the current MQ135 reading.

Example:

```
Bad smell detected! Sensor value: [value]
```

becomes

```
Bad smell detected! Sensor value: 487
```

---

# Supported endpoints

The firmware sends a simple HTTP POST request.

Any webhook endpoint is supported, for example:

- ntfy
- Home Assistant
- Node-RED
- Zapier
- Make.com
- IFTTT
- Custom REST API
- AWS Lambda
- Azure Functions
- Google Cloud Functions

HTTPS endpoints are supported.

---

# Initial setup

On first boot (or after resetting WiFi settings), the device creates a WiFi network:

```
WC-Sensor-Setup
```

Connect using a phone or laptop.

A captive portal will automatically open.

Select your WiFi network and enter the password.

After saving, the device connects automatically.

---

# Web interface

Once connected to your WiFi network, open

```
http://wc-sensor.local
```

If your operating system does not support mDNS, use the IP address shown in the Serial Monitor.

The web interface allows changing:

- Webhook endpoint
- Threshold
- Bad smell message
- Normal message
- Test message

No firmware upload is required.

---

# REST endpoints

## Dashboard

```
GET /
```

Shows:

- Current sensor value
- Current threshold
- Current status
- Configuration page
- Send test notification

---

## Configuration

```
GET /config
```

Configuration page.

---

## Save configuration

```
POST /save
```

Stores configuration in LittleFS.

---

## Device status

```
GET /status
```

Returns:

```json
{
  "value": 287,
  "threshold": 350,
  "bad_smell": false,
  "endpoint": "https://example.com/webhook",
  "ip": "192.168.1.35"
}
```

---

## Test notification

```
GET /test
```

Immediately sends the configured test notification.

---

## Factory reset

```
GET /reset
```

Deletes:

- WiFi credentials
- Saved configuration

and restarts the device.

---

# Button functions

## Short press (FLASH)

Sends the configured test notification.

---

## Long press (5 seconds)

Factory reset:

- Clears WiFi credentials
- Deletes saved configuration
- Reboots into setup mode

---

# Configuration storage

Settings are stored in LittleFS.

The following values persist after reboot:

- Endpoint URL
- Threshold
- Bad smell message
- Normal message
- Test message

---

# Dependencies

- ESP8266 Arduino Core
- WiFiManager
- ArduinoJson
- LittleFS
- ESP8266WebServer
- ESP8266mDNS

---

# Future improvements

- OTA firmware updates
- Authentication for configuration page
- Multiple notification channels
- Sensor calibration wizard
- Moving average filtering
- Hysteresis configuration
- WebSocket live monitoring
- Historical measurements
- MQTT support
- Home Assistant auto-discovery

---

# License

MIT License