# SkyLink

SkyLink is a browser-based drone telemetry dashboard and 3D simulator. The React app runs in a browser and supports demo telemetry without hardware or live telemetry from an STM32 through an ESP32 WebSocket bridge.

## Run the web app

Install [Node.js](https://nodejs.org/) (20 or newer) once. Open PowerShell in the repository folder and install the web dependencies once:

```powershell
cd .\web
npm.cmd install
```

Each time you want to run the app, from the repository folder:

```powershell
cd .\web
npm.cmd run dev
```

Open the local URL Vite prints in the terminal (usually `http://localhost:5173`). The app starts in **Demo telemetry** mode, so you can use the simulator without connecting hardware. Keep the terminal open while using the app; press **Ctrl+C** there to stop the development server.

To use live telemetry, set up and power the hardware below, connect your computer to the ESP32 Wi-Fi network, then choose **Live · ESP32 WebSocket** in the app. The default endpoint is `ws://192.168.4.1:81`; change it in the telemetry-link panel if the ESP32 reports a different IP address.

To create a production build, run this from `web/`:

```powershell
npm.cmd run build
```

The generated static files are written to `web/dist/`.

## Hardware setup

The data path is STM32 UART → ESP32 → WebSocket → browser:

1. Connect the STM32 TX output to ESP32 GPIO 16 (UART2 RX), and connect the grounds. The ESP32 UART uses 9600 baud, 8-N-1. GPIO 17 is configured as UART2 TX but is not used by this one-way link.
2. Ensure the STM32 UART signal is 3.3 V-compatible. Use a level shifter if its TX output is 5 V; do not connect 5 V directly to an ESP32 GPIO.
3. Install **WebSockets by Markus Sattler (Links2004)** through Arduino IDE's Library Manager.
4. Open `firmware/esp32_websocket_bridge/esp32_websocket_bridge.ino` in Arduino IDE, select the **ESP32 Dev Module** board, and upload the sketch.
5. Open Arduino Serial Monitor at **9600 baud**. It prints the Wi-Fi name, IP address, and WebSocket endpoint. The ESP32 creates the `SkyLink` Wi-Fi network using the password configured near the top of the sketch (default: `87654321`).
6. Connect the computer to the `SkyLink` network. The browser app connects to `ws://192.168.4.1:81` by default. The ESP32's optional status page is available at `http://192.168.4.1/`.

Keep the STM32 still during gyro calibration and at the intended zero-altitude reference while its BMP280 pressure baseline is sampled at startup.

## Telemetry format

The STM32 sends one newline-terminated sample at a time:

```text
R:12.35,P:-4.12,Y:0.04,Alt:0.02
```

`R` and `P` are complementary-filtered angles in degrees, `Y` is gyro yaw rate in degrees per second, and `Alt` is relative altitude in metres. The ESP32 broadcasts each completed UART line as a WebSocket text message. Yaw is integrated in the browser and displayed as relative heading, not absolute compass heading. The firmware does not transmit raw accelerometer or gyroscope axes.

The 3D simulator displays a relative vertical range from −1.5 m to +1.5 m. The web app reverses the sensor pitch sign for the model's coordinate convention; the STM32 pitch calculation and transmitted sensor values are unchanged.

## Troubleshooting

- **The app does not open:** Run `npm.cmd run dev` from `web/` and open the URL printed by Vite.
- **The ESP32 Wi-Fi network is missing:** Check that the sketch uploaded and is running. Look for the Wi-Fi name and IP in Arduino Serial Monitor at 9600 baud, then reset the ESP32.
- **Live mode does not connect:** Connect the computer to the ESP32 Wi-Fi network, verify the IP shown in Serial Monitor, install the WebSockets Arduino library, and confirm the browser's WebSocket URL uses port 81.
- **No telemetry appears:** Check STM32 TX-to-GPIO-16 wiring, common ground, UART baud and 8-N-1 settings, and that each STM32 line ends in `\r\n` or `\n`.
- **The production build fails:** Run `npm.cmd install` from `web/`, then retry `npm.cmd run build`.

No Python installation or virtual environment is needed.
