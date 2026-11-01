#include <WiFi.h>
#include <WebServer.h>
#include <vector>

#define STM32_RX 16
#define STM32_TX 17

const char* WIFI_SSID = "scott";
const char* WIFI_PASSWORD = "87654321";
const uint16_t TCP_PORT = 8080;

WebServer webServer(80);
WiFiServer tcpServer(TCP_PORT);
std::vector<WiFiClient> tcpClients;

String currentLine;
String latestData = "Waiting for STM32 data...";

const char webpage[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>STM32 Drone Sensor Monitor</title>
  <style>
    body {
      font-family: Arial, sans-serif;
      background: #101820;
      color: white;
      text-align: center;
      padding: 25px;
    }
    h2 { color: #00e5ff; }
    #data {
      background: #202c39;
      padding: 20px;
      border-radius: 10px;
      font-size: 20px;
      overflow-wrap: anywhere;
    }
    .status { color: #7CFC98; }
  </style>
</head>
<body>
  <h2>STM32 Virtual Drone</h2>
  <p class="status">ESP32 hotspot connected</p>
  <p>Latest sensor reading:</p>
  <div id="data">Waiting for data...</div>
  <p>Auto-refreshing every 250 ms</p>
  <script>
    async function updateData() {
      try {
        const response = await fetch('/data');
        const text = await response.text();
        document.getElementById('data').textContent = text;
      } catch (e) {
        document.getElementById('data').textContent =
          'Waiting for connection...';
      }
    }
    setInterval(updateData, 250);
    updateData();
  </script>
</body>
</html>
)rawliteral";

void acceptTcpClients() {
  WiFiClient client = tcpServer.accept();
  if (client) {
    client.setNoDelay(true);
    tcpClients.push_back(client);
    Serial.println("Python TCP client connected");
  }
}

void forwardSerialByte(uint8_t value) {
  for (auto it = tcpClients.begin(); it != tcpClients.end();) {
    if (!it->connected()) {
      it->stop();
      it = tcpClients.erase(it);
      continue;
    }

    it->write(value);
    ++it;
  }
}

void receiveStm32Telemetry() {
  while (Serial2.available() > 0) {
    const uint8_t value = static_cast<uint8_t>(Serial2.read());
    Serial.write(value);
    forwardSerialByte(value);

    const char c = static_cast<char>(value);
    if (c == '\n') {
      currentLine.trim();
      if (currentLine.length() > 0) {
        latestData = currentLine;
      }
      currentLine = "";
    } else if (c != '\r' && currentLine.length() < 200) {
      currentLine += c;
    }
  }
}

void setup() {
  Serial.begin(9600);
  Serial2.begin(9600, SERIAL_8N1, STM32_RX, STM32_TX);
  delay(1000);

  Serial.println();
  Serial.println("ESP32 UART + Wi-Fi monitor starting...");

  WiFi.mode(WIFI_AP);
  if (!WiFi.softAP(WIFI_SSID, WIFI_PASSWORD)) {
    Serial.println("ERROR: Failed to start Wi-Fi hotspot");
    while (true) {
      delay(1000);
    }
  }

  tcpServer.begin();

  Serial.print("Wi-Fi name: ");
  Serial.println(WIFI_SSID);
  Serial.print("Wi-Fi password: ");
  Serial.println(WIFI_PASSWORD);
  Serial.print("Open monitor at: http://");
  Serial.println(WiFi.softAPIP());
  Serial.print("TCP telemetry port: ");
  Serial.println(TCP_PORT);

  webServer.on("/", HTTP_GET, []() {
    webServer.send_P(200, "text/html", webpage);
  });
  webServer.on("/data", HTTP_GET, []() {
    webServer.send(200, "text/plain", latestData);
  });
  webServer.begin();
  Serial.println("Web server ready.");
}

void loop() {
  acceptTcpClients();
  receiveStm32Telemetry();
  webServer.handleClient();
}
