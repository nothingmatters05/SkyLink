# Build and deployment

Commands below use PowerShell from the repository root unless a directory change is shown. Builds do not require an attached ESP32. **No upload or erase command was run as part of this cleanup.**

## Requirements

- VS Code with the PlatformIO IDE extension or PlatformIO Core.
- PlatformIO Espressif32 platform `8.0.0`.
- Arduino-ESP32 framework package `4.30312.261024` (core version `3.3.12`).
- `links2004/WebSockets` library `2.7.2`.
- Node.js `18+` and npm.
- Python `3.10+` for the standard-library SPIFFS preparation script.

The project is configured for PlatformIO environment `skylink-esp32` and board `esp32dev`. The package pins, 4 MB flash settings, partition CSV, and framework include workaround are in [`../platformio.ini`](../platformio.ini) and [`../scripts/platformio_framework_includes.py`](../scripts/platformio_framework_includes.py).

## Build the frontend and filesystem

```powershell
cd .\web
npm.cmd ci
npm.cmd run build
cd ..
python .\scripts\prepare_spiffs.py
```

`npm.cmd run build` runs `tsc -b` and `vite build`, producing `web/dist/`. The preparation script then:

1. Checks the frontend output, local HTML asset references, aggregate content size, and SPIFFS path lengths.
2. Clears and repopulates the generated `firmware/esp32_websocket_bridge/data/` directory from `web/dist/`.
3. Runs PlatformIO's `buildfs` target.
4. Verifies the image is exactly the configured SPIFFS partition size and lists the expected files with PlatformIO's `mkspiffs`.
5. Atomically installs the verified image as `firmware/esp32_websocket_bridge/spiffs.bin`, retaining the prior image until the candidate passes validation.

The script requires PlatformIO Core to be on `PATH` or installed in its normal per-user environment, and its `tool-mkspiffs` package to be available. The generated `data/`, `.pio/`, `web/dist/`, and `spiffs.bin` are ignored by Git. Do not remove the `data_dir` setting: PlatformIO consumes that directory as the filesystem build input.

## Build ESP32 firmware

With the PlatformIO Core executable installed at its standard Windows path:

```powershell
$pio = Join-Path $env:USERPROFILE ".platformio\penv\Scripts\pio.exe"
& $pio run -e skylink-esp32
```

The build output is under `.pio/build/skylink-esp32/`. Build a filesystem image separately after preparing `data/` with the script:

```powershell
& $pio run -e skylink-esp32 -t buildfs
```

PlatformIO may also be invoked as `pio` if its executable is on `PATH`. `main.c` is STM32 source and is not included in this ESP32 build; no STM32 build project/toolchain is supplied in this repository.

## ESP32 standalone deployment

1. Build the frontend and run `prepare_spiffs.py` as above.
2. Build firmware with `& $pio run -e skylink-esp32`.
3. Set `upload_port` in `platformio.ini` to the ESP32's serial port. The checked-in value may be machine-specific.
4. Upload the firmware with `& $pio run -e skylink-esp32 -t upload`.
5. Upload the SPIFFS contents with `& $pio run -e skylink-esp32 -t uploadfs`.
6. Connect a phone or laptop to the ESP32 AP, then open `http://192.168.4.1/`. Use `http://192.168.4.1/status` to distinguish telemetry reception from dashboard connectivity.

The AP name and password are configured in the sketch. Change the password to a deployment-specific value before using the AP outside a trusted local setup. Set the serial monitor to 9600 baud if using the USB serial console.

`uploadfs` writes the filesystem partition. Confirm the firmware and filesystem use the same [`no_ota.csv`](../partitions/no_ota.csv) layout. Do not select format-on-upload, erase SPIFFS, or change partition offsets/sizes for routine firmware updates. A normal firmware upload and a filesystem upload are separate operations.

## Laptop development fallback

```powershell
cd .\web
npm.cmd ci
npm.cmd run dev
```

Vite starts the dashboard in Demo mode, which works with no hardware. Choose Live mode to connect to the ESP32; when the page is opened on localhost, the default is `ws://192.168.4.1:81`. The development computer must be able to reach that AP address (for example, by connecting to the ESP32 access point).

For a different development endpoint:

```powershell
$env:VITE_TELEMETRY_WS_URL = "ws://192.168.4.1:81"
npm.cmd run dev
```

The application reads this setting only when `import.meta.env.DEV` is true. Production pages served from SPIFFS start in Live mode and automatically use the browser's current host on port 81. The user can switch to Demo mode at any time, including when no ESP32 is available. Do not build production assets with a development override and expect it to change the production endpoint.

## Generated artifacts and deployment checks

- `web/dist/`: Vite production output.
- `firmware/esp32_websocket_bridge/data/`: generated SPIFFS source tree consumed by PlatformIO.
- `.pio/build/skylink-esp32/`: PlatformIO firmware and filesystem build outputs.
- `firmware/esp32_websocket_bridge/spiffs.bin`: verified fixed-size filesystem image for external inspection or upload workflows.

These are generated locally and should not be committed. Check image usage on the device after deployment because SPIFFS filesystem metadata reduces usable capacity below the raw partition size. For failures, see [troubleshooting.md](./troubleshooting.md).
