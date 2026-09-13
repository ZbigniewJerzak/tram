# ESP32 BVG Tram Departures

A small ESP32 application that retrieves the next tram departures from **Erich-Baron-Weg, Berlin** and prints them to the serial monitor once per minute.

The application:

- connects to the Wi-Fi network `birnensaft`;
- reads the Wi-Fi password from LittleFS;
- resolves the BVG/VBB stop ID for `Erich-Baron-Weg` at startup;
- requests tram departures from the BVG transport REST API;
- displays the current Berlin time and the next two non-cancelled departures;
- refreshes the departure overview every 60 seconds.

Example output:

```text
Aktuelle Zeit: 08:21
08:23 +0 Tram 62 -> S Mahlsdorf (2 Minuten)
08:24 +2 Tram 62 -> S Köpenick (3 Minuten)
```

## Requirements

- ESP32 board supported by PlatformIO
- Arduino framework
- Wi-Fi access to `brinensaft`
- PlatformIO Core or the PlatformIO extension for VS Code
- ArduinoJson 7

The current application writes its output to the serial monitor. It does not yet render the information on an e-paper display.

## Project structure

```text
.
├── data/
│   └── wifi-password.txt
├── src/
│   └── main.cpp
└── platformio.ini
```

The application code itself is contained entirely in `src/main.cpp`. The additional password file is kept separate so that the Wi-Fi password is not embedded in the source code.

## PlatformIO configuration

Keep the `board` setting appropriate for the ESP32 board used by the project and add the following settings to the relevant PlatformIO environment:

```ini
[env:your-board]
platform = espressif32
board = your-board-id
framework = arduino

monitor_speed = 115200
board_build.filesystem = littlefs

lib_deps =
    bblanchon/ArduinoJson@^7.0.0
```

For an existing project, normally only `monitor_speed`, `board_build.filesystem`, and the ArduinoJson dependency need to be added.

## Configure the Wi-Fi password

Create the directory `data` in the project root and add:

```text
data/wifi-password.txt
```

The file must contain only the password for `birnensaft`:

```text
YOUR_WIFI_PASSWORD
```

Do not add quotation marks or additional configuration values. Leading and trailing whitespace is removed when the file is read.

### Protect the password from Git

Add the password file to `.gitignore`:

```gitignore
data/wifi-password.txt
```

A safe template file can optionally be committed as `data/wifi-password.example.txt`.

## Build and upload

Upload the LittleFS filesystem first:

```bash
pio run -t uploadfs
```

Then build and upload the firmware:

```bash
pio run -t upload
```

Open the serial monitor:

```bash
pio device monitor
```

The monitor must use **115200 baud**.

When the password changes, upload the filesystem again with `pio run -t uploadfs`.

## Startup sequence

After booting, the application performs these steps:

1. Mounts LittleFS and reads `/wifi-password.txt`.
2. Connects to the Wi-Fi network `birnensaft`.
3. Synchronizes the clock using NTP.
4. Searches the API for the tram stop `Erich-Baron-Weg`.
5. Stores the returned stop ID in memory.
6. Requests and prints the next two tram departures.
7. Repeats the departure request every 60 seconds.

The stop ID is resolved only after startup. It is not searched again during each one-minute refresh.

## Output format

Each departure is printed in the following form:

```text
HH:MM DELAY Tram LINE -> DIRECTION (WAITING_TIME)
```

Example:

```text
08:24 +2 Tram 62 -> S Köpenick (3 Minuten)
```

The fields mean:

- `08:24`: expected departure time, including real-time information when available;
- `+2`: delay in minutes;
- `62`: tram line;
- `S Köpenick`: destination reported by the API;
- `3 Minuten`: remaining time calculated using the ESP32 clock.

Cancelled departures are ignored. If no real-time timestamp is available, the planned departure time is used.

## Time handling

The ESP32 synchronizes its clock with:

- `pool.ntp.org`
- `time.cloudflare.com`

The timezone is configured for Berlin, including automatic transitions between CET and CEST.

## API requests

The application uses these endpoints:

```text
GET https://v6.bvg.transport.rest/locations
GET https://v6.bvg.transport.rest/stops/{stopId}/departures
```

Departure queries are restricted to trams and a 90-minute time window. Up to ten API results are inspected so that the application can select the next two valid departures.

## Error behavior

- If the password file is missing or empty, the application stops and prints an error.
- If Wi-Fi is disconnected, the application reconnects before the next request.
- If the stop cannot be resolved, the application retries every five seconds.
- If a departure request fails, an error is printed and the next regular refresh occurs after one minute.
- If no valid tram departure is available, the application reports that no departures were found within the next 90 minutes.

## Troubleshooting

### `Missing /wifi-password.txt in LittleFS`

Create `data/wifi-password.txt` and run:

```bash
pio run -t uploadfs
```

### The device repeatedly prints dots while connecting

Check:

- whether `birnensaft` is available;
- whether the password file contains the correct password;
- whether the ESP32 can use the network's Wi-Fi frequency and authentication mode.

### The clock never synchronizes

Confirm that the network permits DNS, NTP, and internet access. The application waits for successful NTP synchronization before continuing.

### Umlauts are displayed incorrectly

Use a terminal configured for UTF-8. The API may return destination names such as `S Köpenick`.

### HTTPS or API requests fail

The prototype uses `WiFiClientSecure::setInsecure()`, which disables TLS certificate validation. This avoids certificate-management complexity but is not appropriate for a hardened production deployment. Production firmware should validate the server certificate or certificate authority.

## Current limitations

- Output is written only to the serial monitor.
- The Wi-Fi SSID and stop name are compile-time constants.
- The stop ID is stored only in RAM and is resolved again after every reboot.
- HTTPS server certificates are not validated.
- Failed API responses are not cached; the previous departure overview is not retained.
- The application blocks while connecting to Wi-Fi and synchronizing time.

## Source file

Place the supplied application source at:

```text
src/main.cpp
```
