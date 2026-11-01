# Drone Telemetry Visualizer

A two-part telemetry project. The ESP32 creates its own Wi-Fi hotspot, serves a browser-based sensor monitor, and broadcasts UART telemetry to the Python visualizer over TCP.

- `firmware/esp32_uart_tcp_bridge/esp32_uart_tcp_bridge.ino` receives telemetry from an STM32 over UART, hosts a browser monitor, and broadcasts telemetry to TCP clients.
- `visualizer/drone_visualizer.py` reads the TCP stream in a background thread and renders the drone attitude and altitude with VPython.

## Telemetry format

Send one newline-terminated line for each sample:

```text
R:12.35,P:-4.12,Y:0.04,Alt:0.02
```

`R` and `P` are absolute angles in degrees, `Y` is yaw rate in degrees per second, and `Alt` is altitude.
The ESP32 forwards each UART byte to both the Serial Monitor and TCP clients without filtering. The visualizer prints the original received frame alongside its parsed values; it does not smooth the sensor data. It integrates yaw rate for the displayed heading and clamps altitude only for the 3D model, to the 0-2 m flight envelope.

## Hardware and network setup

1. Connect the STM32 TX output to ESP32 GPIO 16 (UART2 RX). Connect the grounds together. The sketch configures UART at 9600 baud, 8-N-1. GPIO 17 is configured as UART2 TX but is not used by this one-way bridge.
2. Ensure the STM32 UART signal is 3.3 V-compatible. Use a level shifter if its TX output is 5 V; do not connect 5 V directly to an ESP32 GPIO.
3. Upload `firmware/esp32_uart_tcp_bridge/esp32_uart_tcp_bridge.ino` from Arduino IDE with the **ESP32 Dev Module** board selected. The sketch creates the `scott` hotspot using the password configured at the top of the sketch.
4. Open Arduino Serial Monitor at **9600 baud**. It prints the hotspot name and IP address (normally `192.168.4.1`).
5. On your computer, connect to the ESP32 Wi-Fi hotspot `scott` with password `87654321`. The ESP32 does not need to join a router or the internet for this setup.
6. To check UART reception in a browser, visit `http://192.168.4.1/`. The page displays the latest complete line. TCP telemetry for the visualizer is available on port **8080** at the same IP.

## Run the Python visualizer

Use Python 3.12 for the visualizer. From the project directory, create the environment and install the requirements once:

```powershell
py -3.12 -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r requirements.txt
```

To run the visualizer, use the venv's Python directly. Activation is not required:

```powershell
.\.venv\Scripts\python.exe .\visualizer\drone_visualizer.py
```

If you prefer the shorter `python` command, activate the environment first. If PowerShell prevents activation, allow it for the current terminal with `Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass`, then run `.\.venv\Scripts\Activate.ps1`.

The visualizer defaults to the ESP32 hotspot IP `192.168.4.1`. If your Serial Monitor prints a different IP, edit `ESP_IP` near the top of `visualizer/drone_visualizer.py`. Make sure the computer is connected to the ESP32 hotspot before launching.

The terminal prints each valid sample as the raw, unfiltered STM32 frame and its parsed values. The 3D view updates at 60 frames per second in a closed, translucent 4 m x 4 m x 2 m flight box. Roll and pitch are used as received, yaw rate is integrated into the displayed heading, and altitude is clamped to 0-2 m for the model. The drone has animated counter-rotating propellers. Press **Ctrl+C** in the terminal to stop.

## Troubleshooting

- **Hotspot `scott` is not visible:** Confirm the sketch uploaded and is running. Check the Serial Monitor at 9600 baud for `Wi-Fi name: scott` and the hotspot IP. Press the ESP32 EN/Reset button after opening the monitor.
- **Connection refused / timeout:** Connect the computer to the ESP32 hotspot, check the IP in the Serial Monitor and `ESP_IP`, and allow TCP port 8080 through any local firewall.
- **No telemetry updates:** Check STM32 TX-to-GPIO-16 wiring, common ground, UART baud and 8-N-1 settings, and that each STM32 line ends in `\r\n` or `\n`.
- **Missing VPython:** Run `.\.venv\Scripts\python.exe -m pip install -r requirements.txt`.
