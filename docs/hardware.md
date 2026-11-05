# Hardware

This document records only assignments supported by the project brief or source. Confirm the exact board variants and breakout-board power circuitry before wiring.

## Boards and sensors

| Item | Project information |
|---|---|
| STM32 side | Sensor firmware uses HK32F103/STM32F103-compatible register addresses. The exact board and MCU part number are not identified in the repository. |
| ESP32 side | Project brief identifies an ESP32-D0WD-V3 development board. PlatformIO selects the generic `esp32dev` target; verify the actual module/board before flashing. |
| IMU | MPU6050-compatible accelerometer/gyroscope register map, I2C address `0x68`. |
| Barometer | BMP280, I2C address `0x76` or `0x77` (probed in that order). |
| Cables | USB cables compatible with the respective development-board connectors; connector types are not specified here. |

## Signal wiring

| From | To | Notes |
|---|---|---|
| STM32 I2C1 PB6 | MPU6050 SCL and BMP280 SCL | Shared I2C clock. |
| STM32 I2C1 PB7 | MPU6050 SDA and BMP280 SDA | Shared I2C data. |
| STM32 USART1 PA9 (TX) | ESP32 UART2 GPIO16 (RX) | One-way telemetry, 9600 baud, 8-N-1. |
| STM32 GND | ESP32 GND | Required common reference for UART. |
| ESP32 UART2 GPIO17 (TX) | No connection required | Configured as UART2 TX but unused by this one-way protocol. |

The source does not assign a sensor breakout power pin or document pull-up resistors. Follow the specific breakout-board documentation for supply voltage, logic levels, and I2C pull-ups. Do not feed a 5 V UART signal into the ESP32; use compatible 3.3 V logic or a suitable level shifter. Do not power boards by connecting their UART signals.

## Software-configured ESP32 hardware

PlatformIO selects `board = esp32dev`, 4 MB flash, DIO flash mode, and an 80 MHz flash frequency. The active partition layout in [`../partitions/no_ota.csv`](../partitions/no_ota.csv) is:

| Partition | Offset | Size |
|---|---:|---:|
| NVS | `0x9000` | `0x5000` |
| OTA metadata | `0xE000` | `0x2000` |
| Application (`app0`) | `0x10000` | `0x200000` |
| SPIFFS | `0x210000` | `0x1E0000` |
| Core dump | `0x3F0000` | `0x10000` |

The `no_ota.csv` table provides one application slot and a `0x1E0000`-byte SPIFFS partition. Keep this table aligned with the firmware and filesystem build; do not substitute a different layout without revalidating both images.

The Wi-Fi AP name and password are constants in the ESP32 sketch. Set a unique deployment password before deploying to a shared environment. The default credentials are intentionally not reproduced here.
