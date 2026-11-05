# Troubleshooting

## ESP32 not detected or wrong serial port

- Check the USB cable supports data and that the board is powered.
- Install the USB-to-serial driver required by the specific board.
- Find the current device port in the operating system and set `upload_port` and, if needed, `monitor_port` in `platformio.ini`.
- Close other serial monitors before uploading.
- Do not assume the repository's configured port is correct for another computer.

## PlatformIO build failures

- Open the repository root containing `platformio.ini`, not `pio_src/`.
- Let PlatformIO install the pinned Espressif32 platform, Arduino framework, and WebSockets dependency.
- Keep the framework include workaround in `scripts/platformio_framework_includes.py`; it adds Arduino framework library include directories required by the selected package.
- Build the frontend before running `python .\scripts\prepare_spiffs.py`; the script expects `web/dist/` to exist.
- If PlatformIO cannot find the filesystem input, run the preparation script to regenerate the ignored `firmware/esp32_websocket_bridge/data/` directory.
- For frontend issues, run `npm.cmd ci` in `web/`, then `npm.cmd run build`.

## SPIFFS mount failure or missing website

- Check Serial Monitor output at 9600 baud for the SPIFFS mount result and reported capacity.
- Verify that the filesystem image uses `partitions/no_ota.csv` and that the filesystem upload completed.
- The site files must include `index.html` and every asset referenced by it. Re-run the frontend build and `prepare_spiffs.py`.
- Confirm the image fits the `0x1E0000`-byte partition; filesystem metadata means the usable capacity is lower than the partition size.
- The firmware mounts with formatting disabled. A mount failure does not format or repair the filesystem; rebuild and re-upload the filesystem after resolving the cause.

## Website does not load or assets return 404

- Connect the client device to the ESP32 access point and open `http://192.168.4.1/`.
- If `/status` responds but the dashboard does not, check that SPIFFS contains `index.html`.
- Rebuild the frontend and filesystem as documented in [build_and_deployment.md](./build_and_deployment.md); upload the entire filesystem image, not only `index.html`.
- Missing JavaScript/CSS paths return 404 intentionally. Rebuild and upload a matching set of hashed assets rather than relying on SPA fallback.
- Browser cache can retain an older page; refresh after uploading a matching firmware/filesystem pair.

## WebSocket connection failure

- Confirm the ESP32 AP is running and the browser can reach it.
- The endpoint is `ws://192.168.4.1:81` on the default AP or `ws://<ESP32-host>:81` when served from another network.
- In local Vite development, choose Live mode and verify the configured URL. The optional `VITE_TELEMETRY_WS_URL` override is development-only.
- A successful WebSocket handshake does not mean telemetry is being received; inspect `/status`.

## No telemetry, invalid packets, or stale data

- Verify STM32 PA9 TX is connected to ESP32 GPIO16 RX and the boards share ground.
- Confirm UART settings are 9600 baud, 8-N-1, and the signal presented to the ESP32 is 3.3 V-compatible.
- Ensure frames end with a newline and contain exactly one finite number for each of `R`, `P`, `Y`, and `Alt`.
- Unknown/duplicate/missing fields, non-numeric or non-finite values, and lines over 200 characters are rejected.
- Inspect `/status`: `telemetry` indicates whether a valid packet has ever been received; `age_ms` gives the latest valid sample's age; `last_packet_valid` and `last_frame` describe the latest complete non-empty frame.
- The browser marks telemetry stale after 2 seconds without a valid update. Check that the STM32 is sampling and transmitting continuously.

## Sensor or wiring problems

- Check common ground and the sensor breakouts' power/logic requirements against their documentation.
- Verify the MPU6050-compatible I2C address (`0x68`) and BMP280 address (`0x76` or `0x77`) and I2C connections PB6/PB7.
- Keep the sensor board stationary during the startup gyro calibration and pressure baseline capture.
- Do not infer sensor health from dashboard demo mode; it is simulated data.

## Local frontend cannot reach the ESP32

- Demo mode does not need Wi-Fi or an ESP32.
- To test live mode, connect the laptop to the ESP32 AP or use another route to reach its WebSocket address.
- If the Vite page is on `localhost`, its default live URL is `ws://192.168.4.1:81`; use the URL field or development environment override when needed.
- A restrictive browser/network policy may prevent the local page from reaching the AP; use the browser console and dashboard error message to identify the connection failure.
