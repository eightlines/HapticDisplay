# HapticDisplay

A powered haptic feedback knob with a circular display on top. The display is driven by an ESP32-P4 and wired through the motor's hollow shaft, so the screen sits inside a rotating knurled interface. UI elements are arranged around the display and selected with the knob, and the haptic detents line up with the visuals so they appear to lock into place.

## Status

This repo currently contains the knob firmware for the ESP32. The ESP32-P4 display work has not been integrated yet.

| Phase | Goal | State |
| --- | --- | --- |
| 1. Prototype | Merge the knob firmware and ESP32-P4 display work into a common interface | In progress |
| 2. Circuit board | Design an SMD board covering the ESP32, screen interface, BLDC driver and power, for manufacture with pick and place | Planned |
| 3. Case | 3D printed casing | Planned |

The display work will build on [ESP32Rive](https://github.com/eightlines/ESP32Rive) and [PocketStream](https://github.com/eightlines/PocketStream).

## Hardware

- Elegoo ESP32S DevKit V1
- 2804 hollow shaft BLDC motor (7 pole pairs)
- AS5600 magnetic encoder
- ZORZA mini driver board (DRV8313)
- 12 VDC power supply

### Wiring

| ESP32 pin | Connects to |
| --- | --- |
| GPIO 27 | Driver IN1 |
| GPIO 26 | Driver IN2 |
| GPIO 25 | Driver IN3 |
| GPIO 33 | Driver EN |
| GPIO 22 | AS5600 SDA |
| GPIO 21 | AS5600 SCL |

## Getting started

You'll need [PlatformIO](https://platformio.org/).

1. Copy `.env.example` to `.env` and fill in your WiFi details:

   ```
   WIFI_AP_SSID="your-access-point-name"
   WIFI_AP_PASSWORD="your-access-point-password"
   WIFI_STA_SSID="your-wifi-name"
   WIFI_STA_PASSWORD="your-wifi-password"
   ```

   The access point password needs at least 8 characters. Real environment variables with the same names override `.env`.

2. Build and upload:

   ```
   pio run --target upload
   ```

3. Open the serial monitor at 115200 baud. It prints the address of the web interface at startup.

   ```
   pio device monitor
   ```

The motor moves during startup while SimpleFOC calibrates the sensor, so leave the knob free to turn.

`.env` is ignored by git and must stay out of commits. At build time, `load_env.py` turns it into a generated `wifi_secrets.h` in the build folder.

### WiFi modes

With `USE_STATION_MODE` set to `true` in `src/main.cpp`, the knob joins your network and falls back to its own access point if the connection fails. With it set to `false`, the knob always creates its own access point, at `http://192.168.4.1`.

In access point mode, the firmware prints the password to the serial monitor.

## Using the knob

The firmware runs SimpleFOC in torque mode. Each detent is a spring force pulling towards the nearest position, with damping and a small dead zone at the centre to reduce heat.

Tune the feel live from the web interface or the API below. Changes aren't saved and reset to the defaults on reboot. Over serial, send `+` or `-` to add or remove a detent.

## API

The web server runs on port 80 with CORS open to all origins and no authentication, so only run it on a network you trust.

| Method | Route | Purpose |
| --- | --- | --- |
| `GET` | `/` | Web interface |
| `GET` | `/api/data` | Live `angle`, `velocity`, `detent` and `numDetents` |
| `GET` | `/api/params` | Current tuning values |
| `POST` | `/api/params` | Update tuning values with a JSON body |

Values outside these ranges are ignored:

| Parameter | Default | Range |
| --- | --- | --- |
| `numDetents` | 12 | 2 to 36 |
| `detentStrength` | 1.5 | 0 to 5 |
| `deadZone` | 0.03 | 0 to 0.2 |
| `springStiffness` | 0.2 | 0.1 to 3 |
| `damping` | 0.02 | 0 to 0.2 |
| `overshootAllow` | 0.8 | 0.1 to 1 |

Example:

```
curl -X POST http://<knob-ip>/api/params \
  -H "Content-Type: application/json" \
  -d '{"numDetents": 24, "detentStrength": 2}'
```
