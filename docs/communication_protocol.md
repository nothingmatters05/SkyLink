# Communication protocol

## STM32 to ESP32 UART

- UART: 9600 baud, 8 data bits, no parity, 1 stop bit (8-N-1).
- STM32 TX: USART1 PA9.
- ESP32 RX: UART2 GPIO16; UART2 TX is configured on GPIO17 but unused.
- Framing: one ASCII text record terminated by line feed (`\n`); carriage return is ignored.

Example emitted by the STM32:

```text
R:12.35,P:-4.12,Y:0.04,Alt:0.02
```

| Key | Meaning | Unit |
|---|---|---|
| `R` | Roll | degrees |
| `P` | Pitch | degrees |
| `Y` | Z-axis yaw angular rate | degrees per second |
| `Alt` | Relative barometric altitude from the STM32 startup pressure reference | metres |

All four keys must occur exactly once and have finite numeric values. The parser accepts fields in any order and trims surrounding whitespace. It rejects unknown or duplicate keys, missing fields, malformed values, non-finite numbers, and lines longer than 200 characters. Empty lines are ignored. The ESP32 broadcasts only valid complete frames; the browser parses and validates text frames again.

The example's ordering is the ordering emitted by the current STM32 source. `Y` is a rate, not absolute heading. The dashboard integrates this rate into a relative display heading; that heading is resettable and session-local.

## ESP32 network interfaces

| Interface | Address | Behavior |
|---|---|---|
| HTTP | TCP port 80 | Serves the dashboard, assets, and `/status`. |
| WebSocket | TCP port 81 | Broadcasts valid telemetry frames as text messages. |
| Default AP address | `192.168.4.1` | ESP32 soft-AP default address. |

### HTTP routes

- `GET /` serves the dashboard `index.html`.
- Existing static paths are served from SPIFFS with an extension-based MIME type.
- `GET /status` returns JSON with:
  - `telemetry`: whether any valid sample has been received.
  - `data`: latest valid frame or a waiting message.
  - `age_ms`: age of the latest valid sample, or `null`.
  - `last_packet_valid`: validity of the most recently completed non-empty frame, or `null` before any frame.
  - `last_frame`: latest frame text or an oversized-frame indicator.
  - `wifi_clients`, `ssid`, and `ip`: AP connection count and network information.
- Extensionless paths fall back to `index.html` for client-side routing. Missing asset/file paths return 404 rather than the SPA document.

The status endpoint is an operational snapshot, not an authenticated control interface. A WebSocket connection by itself does not imply that telemetry is arriving.

## Browser connection behavior

The hosted production page uses `ws://<page-host>:81`. Local Vite development defaults to `ws://192.168.4.1:81`; `VITE_TELEMETRY_WS_URL` may override that endpoint in development only. The dashboard also exposes a live URL field. A live link is marked stale after 2 seconds without a valid sample.
