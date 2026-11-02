#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>

// Connect the STM32's 3.3 V TX signal to ESP32 GPIO 16; GPIO 17 is the return TX pin.
constexpr uint8_t STM32_RX_PIN = 16;
constexpr uint8_t STM32_TX_PIN = 17;
constexpr uint32_t UART_BAUD_RATE = 9600;
constexpr size_t MAX_TELEMETRY_LINE_LENGTH = 200;

const char* WIFI_SSID = "SkyLink";
const char* WIFI_PASSWORD = "87654321";
const uint16_t WEBSOCKET_PORT = 81;

WebServer webServer(80);
WebSocketsServer webSocket(WEBSOCKET_PORT);

// Buffer one newline-terminated STM32 frame before broadcasting it to WebSocket clients.
String currentLine;
String latestData = "Waiting for STM32 data...";
bool currentLineOverflowed = false;
bool hasTelemetry = false;
uint32_t lastTelemetryAt = 0;

const char webpage[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">
  <meta name="theme-color" content="#07111b">
  <title>SkyLink | Flight Telemetry</title>
  <style>
    :root {
      color-scheme: dark;
      --bg: #07111b;
      --panel: #0c1925;
      --line: #1b2b39;
      --muted: #8293a2;
      --text: #e7eef4;
      --cyan: #62d9e8;
      --green: #63dda5;
      --amber: #f3bf69;
    }
    * { box-sizing: border-box; }
    body {
      margin: 0;
      min-height: 100vh;
      background: radial-gradient(ellipse at 50% -20%, #142d3c 0, var(--bg) 58%);
      color: var(--text);
      font: 14px/1.5 Inter, ui-sans-serif, system-ui, -apple-system, "Segoe UI", sans-serif;
    }
    .shell { width: min(100% - 40px, 1040px); margin: 0 auto; padding: 34px 0 28px; }
    header { display: flex; align-items: center; justify-content: space-between; gap: 20px; margin-bottom: 34px; }
    .brand { display: flex; align-items: center; gap: 12px; }
    .mark {
      display: grid; place-items: center; width: 38px; height: 38px;
      border: 1px solid #2b5361; border-radius: 11px; background: #102a36; color: var(--cyan);
    }
    .brand-name { font-size: 16px; font-weight: 700; letter-spacing: .12em; }
    .brand-sub { color: var(--muted); font-size: 11px; letter-spacing: .14em; text-transform: uppercase; }
    .live-badge {
      display: inline-flex; align-items: center; gap: 9px; padding: 8px 12px;
      border: 1px solid var(--line); border-radius: 999px; background: #0b1722;
      color: var(--muted); font-size: 11px; font-weight: 700; letter-spacing: .09em;
    }
    .dot { width: 7px; height: 7px; border-radius: 50%; background: var(--amber); }
    .live-badge.live { color: var(--green); border-color: #23513f; }
    .live-badge.live .dot { background: var(--green); animation: pulse 1.8s ease-out infinite; }
    .live-badge.stale .dot { background: var(--amber); }
    @keyframes pulse {
      0% { box-shadow: 0 0 0 0 #63dda566; }
      70% { box-shadow: 0 0 0 7px #63dda500; }
      100% { box-shadow: 0 0 0 0 #63dda500; }
    }
    .eyebrow { margin: 0 0 7px; color: var(--cyan); font-size: 10px; font-weight: 700; letter-spacing: .18em; text-transform: uppercase; }
    h1 { margin: 0; font-size: clamp(25px, 4vw, 36px); font-weight: 600; letter-spacing: -.04em; }
    .intro { margin: 7px 0 25px; color: var(--muted); }
    .metrics { display: grid; grid-template-columns: repeat(4, minmax(0, 1fr)); gap: 12px; }
    .card {
      position: relative; overflow: hidden; min-height: 142px; padding: 18px;
      border: 1px solid var(--line); border-radius: 13px;
      background: linear-gradient(145deg, #10202d, var(--panel) 70%);
      transition: border-color .2s, transform .2s;
    }
    .card:hover { transform: translateY(-2px); border-color: #31505e; }
    .card::after {
      position: absolute; right: -25px; bottom: -50px; width: 110px; height: 110px;
      border: 1px solid #244150; border-radius: 50%; content: "";
      box-shadow: 0 0 0 12px #24415022, 0 0 0 25px #24415012;
    }
    .metric-head { display: flex; align-items: center; gap: 9px; color: var(--muted); font-size: 12px; }
    .metric-icon { width: 20px; color: var(--cyan); font-size: 15px; text-align: center; }
    .value { position: relative; z-index: 1; margin-top: 18px; font-size: clamp(27px, 4vw, 36px); font-variant-numeric: tabular-nums; font-weight: 600; letter-spacing: -.04em; }
    .unit { margin-left: 5px; color: var(--muted); font-size: 12px; font-weight: 500; letter-spacing: 0; }
    .section-label { margin: 27px 0 10px; color: var(--muted); font-size: 10px; font-weight: 700; letter-spacing: .15em; text-transform: uppercase; }
    .details { display: grid; grid-template-columns: 1.5fr 1fr 1fr; gap: 12px; }
    .detail {
      min-width: 0; padding: 14px 16px; border: 1px solid var(--line);
      border-radius: 11px; background: #0a1621;
    }
    .detail-label { color: var(--muted); font-size: 11px; }
    .detail-value { margin-top: 4px; overflow: hidden; color: var(--text); font-size: 13px; font-variant-numeric: tabular-nums; text-overflow: ellipsis; white-space: nowrap; }
    .frame { margin-top: 12px; padding: 15px 16px; border: 1px solid var(--line); border-radius: 11px; background: #0a1621; }
    .frame-label { color: var(--muted); font-size: 10px; font-weight: 700; letter-spacing: .12em; text-transform: uppercase; }
    #raw { display: block; margin-top: 7px; overflow-wrap: anywhere; color: #b7c8d4; font: 12px/1.6 ui-monospace, SFMono-Regular, Consolas, monospace; }
    footer { display: flex; justify-content: space-between; gap: 12px; margin-top: 22px; color: #627586; font-size: 10px; }
    @media (max-width: 680px) {
      .shell { width: min(100% - 28px, 520px); padding-top: 22px; }
      header { margin-bottom: 28px; }
      .metrics { grid-template-columns: repeat(2, minmax(0, 1fr)); gap: 9px; }
      .card { min-height: 126px; padding: 14px; }
      .details { grid-template-columns: 1fr 1fr; gap: 9px; }
      .detail:first-child { grid-column: 1 / -1; }
      footer { flex-direction: column; }
    }
    @media (prefers-reduced-motion: reduce) {
      *, *::before, *::after { animation-duration: .01ms !important; transition-duration: .01ms !important; }
    }
  </style>
</head>
<body>
  <main class="shell">
    <header>
      <div class="brand">
        <div class="mark" aria-hidden="true">
          <svg width="20" height="20" viewBox="0 0 24 24" fill="none">
            <path d="M12 2.8 14.4 9.6 21.2 12l-6.8 2.4L12 21.2l-2.4-6.8L2.8 12l6.8-2.4L12 2.8Z" stroke="currentColor" stroke-width="1.5" stroke-linejoin="round"/>
            <circle cx="12" cy="12" r="2" fill="currentColor"/>
          </svg>
        </div>
        <div><div class="brand-name">SKYLINK</div><div class="brand-sub">Flight Systems</div></div>
      </div>
      <div id="stream" class="live-badge"><span class="dot"></span><span id="stream-label">WAITING FOR DATA</span></div>
    </header>

    <p class="eyebrow">Live telemetry</p>
    <h1>Flight data</h1>
    <p class="intro">Real-time attitude and altitude from the onboard sensor link.</p>

    <section class="metrics" aria-label="Flight telemetry">
      <article class="card"><div class="metric-head"><span class="metric-icon">↻</span>Roll</div><div class="value"><span id="roll">--</span><span class="unit">°</span></div></article>
      <article class="card"><div class="metric-head"><span class="metric-icon">↕</span>Pitch</div><div class="value"><span id="pitch">--</span><span class="unit">°</span></div></article>
      <article class="card"><div class="metric-head"><span class="metric-icon">⟳</span>Yaw rate</div><div class="value"><span id="yaw">--</span><span class="unit">°/s</span></div></article>
      <article class="card"><div class="metric-head"><span class="metric-icon">⌖</span>Altitude</div><div class="value"><span id="altitude">--</span><span class="unit">m</span></div></article>
    </section>

    <p class="section-label">Link status</p>
    <section class="details" aria-label="Link status">
      <div class="detail"><div class="detail-label">Access point</div><div class="detail-value" id="network">Connecting…</div></div>
      <div class="detail"><div class="detail-label">Telemetry age</div><div class="detail-value" id="age">--</div></div>
      <div class="detail"><div class="detail-label">WebSocket port</div><div class="detail-value">81</div></div>
    </section>
    <div class="frame"><div class="frame-label">Latest received frame</div><code id="raw">Waiting for STM32 telemetry…</code></div>
    <footer><span>SkyLink · ESP32 UART bridge</span><span>Refreshes automatically</span></footer>
  </main>
  <script>
    const fields = { R: 'roll', P: 'pitch', Y: 'yaw', Alt: 'altitude' };
    function showTelemetry(line) {
      const values = {};
      line.split(',').forEach((part) => {
        const separator = part.indexOf(':');
        if (separator < 0) return;
        const key = part.slice(0, separator).trim();
        const value = Number(part.slice(separator + 1).trim());
        if (fields[key] && Number.isFinite(value)) values[key] = value;
      });
      Object.entries(fields).forEach(([key, id]) => {
        document.getElementById(id).textContent =
          values[key] === undefined ? '--' : values[key].toFixed(2);
      });
    }
    async function updateStatus() {
      try {
        const response = await fetch('/status', { cache: 'no-store' });
        if (!response.ok) throw new Error('Status request failed: ' + response.status);
        const status = await response.json();
        const hasData = status.telemetry;
        const age = hasData ? status.age_ms : null;
        const live = hasData && age < 2500;
        const badge = document.getElementById('stream');
        badge.className = 'live-badge' + (live ? ' live' : hasData ? ' stale' : '');
        document.getElementById('stream-label').textContent =
          live ? 'STREAM LIVE' : hasData ? 'SIGNAL STALE' : 'WAITING FOR DATA';
        document.getElementById('network').textContent =
          status.ssid + ' · ' + status.ip + ' · ' + status.wifi_clients + ' connected';
        document.getElementById('age').textContent =
          hasData ? (age < 1000 ? age + ' ms ago' : (age / 1000).toFixed(1) + ' s ago') : 'No samples received';
        document.getElementById('raw').textContent = hasData ? status.data : 'Waiting for STM32 telemetry…';
        if (hasData) showTelemetry(status.data);
      } catch (e) {
        document.getElementById('stream').className = 'live-badge';
        document.getElementById('stream-label').textContent = 'ESP32 UNREACHABLE';
        document.getElementById('network').textContent = 'Connection unavailable';
      }
    }
    setInterval(updateStatus, 500);
    updateStatus();
  </script>
</body>
</html>
)rawliteral";

String jsonEscape(const String& value) {
  String escaped;
  escaped.reserve(value.length() + 8);
  for (size_t i = 0; i < value.length(); ++i) {
    const char c = value[i];
    if (c == '"' || c == '\\') {
      escaped += '\\';
      escaped += c;
    } else if (c == '\n') {
      escaped += F("\\n");
    } else if (c == '\r') {
      escaped += F("\\r");
    } else if (c == '\t') {
      escaped += F("\\t");
    } else if (static_cast<uint8_t>(c) < 0x20) {
      escaped += ' ';
    } else {
      escaped += c;
    }
  }
  return escaped;
}

void receiveStm32Telemetry() {
  // Forward bytes unchanged to Serial; discard oversized frames until their newline.
  while (Serial2.available() > 0) {
    const uint8_t value = static_cast<uint8_t>(Serial2.read());
    Serial.write(value);

    const char c = static_cast<char>(value);
    if (c == '\n') {
      if (!currentLineOverflowed) {
        currentLine.trim();
        if (currentLine.length() > 0) {
          latestData = currentLine;
          hasTelemetry = true;
          lastTelemetryAt = millis();
          webSocket.broadcastTXT(currentLine);
        }
      }
      currentLine = "";
      currentLineOverflowed = false;
    } else if (c != '\r') {
      if (currentLine.length() < MAX_TELEMETRY_LINE_LENGTH) {
        currentLine += c;
      } else {
        currentLineOverflowed = true;
      }
    }
  }
}

void setup() {
  Serial.begin(9600);
  Serial2.begin(UART_BAUD_RATE, SERIAL_8N1, STM32_RX_PIN, STM32_TX_PIN);
  delay(1000);

  Serial.println();
  Serial.println("ESP32 UART + WebSocket bridge starting...");

  WiFi.mode(WIFI_AP);
  if (!WiFi.softAP(WIFI_SSID, WIFI_PASSWORD)) {
    Serial.println("ERROR: Failed to start Wi-Fi hotspot");
    while (true) {
      delay(1000);
    }
  }

  webSocket.begin();

  Serial.print("Wi-Fi name: ");
  Serial.println(WIFI_SSID);
  Serial.print("Open monitor at: http://");
  Serial.println(WiFi.softAPIP());
  Serial.print("WebSocket telemetry endpoint: ws://");
  Serial.print(WiFi.softAPIP());
  Serial.print(":");
  Serial.println(WEBSOCKET_PORT);

  webServer.on("/", HTTP_GET, []() {
    webServer.send_P(200, "text/html; charset=utf-8", webpage);
  });
  webServer.on("/status", HTTP_GET, []() {
    String response;
    response.reserve(latestData.length() + 140);
    response = "{\"telemetry\":";
    response += hasTelemetry ? "true" : "false";
    response += ",\"data\":\"";
    response += jsonEscape(latestData);
    response += "\",\"age_ms\":";
    if (hasTelemetry) {
      response += String(static_cast<uint32_t>(millis() - lastTelemetryAt));
    } else {
      response += "null";
    }
    response += ",\"wifi_clients\":";
    response += String(WiFi.softAPgetStationNum());
    response += ",\"ssid\":\"";
    response += jsonEscape(WIFI_SSID);
    response += "\",\"ip\":\"";
    response += WiFi.softAPIP().toString();
    response += "}";
    webServer.send(200, "application/json", response);
  });
  webServer.begin();
  Serial.println("Web server ready.");
}

void loop() {
  receiveStm32Telemetry();
  webSocket.loop();
  webServer.handleClient();
}
