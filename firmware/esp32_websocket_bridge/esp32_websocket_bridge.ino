#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <SPIFFS.h>
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

// Connect STM32 USART1 TX (PA9) to ESP32 UART2 RX (GPIO 16); GPIO 17 is unused.
constexpr uint8_t STM32_RX_PIN = 16;
constexpr uint8_t STM32_TX_PIN = 17;
constexpr uint32_t UART_BAUD_RATE = 9600;
// Bound each newline-delimited UART frame before parsing or broadcasting it.
constexpr size_t MAX_TELEMETRY_LINE_LENGTH = 200;

const char* WIFI_SSID = "SkyLink";
const char* WIFI_PASSWORD = "87654321";
const uint16_t WEBSOCKET_PORT = 81;

WebServer webServer(80);
WebSocketsServer webSocket(WEBSOCKET_PORT);

String currentLine;
String latestData = "Waiting for STM32 data...";
String lastFrame;
bool currentLineOverflowed = false;
bool hasTelemetry = false;
bool hasLastFrame = false;
bool lastFrameValid = false;
bool filesystemReady = false;
bool accessPointReady = false;
uint32_t lastTelemetryAt = 0;
uint32_t nextAccessPointRetryAt = 0;

bool parseTelemetryFrame(const String& frame) {
  if (frame.length() == 0 || frame.length() > MAX_TELEMETRY_LINE_LENGTH) {
    return false;
  }

  char buffer[MAX_TELEMETRY_LINE_LENGTH + 1];
  frame.toCharArray(buffer, sizeof(buffer));
  bool seen[4] = {false, false, false, false};
  size_t fieldCount = 0;
  char* field = buffer;

  while (field != nullptr) {
    char* next = strchr(field, ',');
    if (next != nullptr) {
      *next = '\0';
    }

    char* separator = strchr(field, ':');
    if (separator == nullptr || strchr(separator + 1, ':') != nullptr) {
      return false;
    }
    *separator = '\0';

    char* key = field;
    while (isspace(static_cast<unsigned char>(*key))) {
      ++key;
    }
    char* keyEnd = key + strlen(key);
    while (keyEnd > key && isspace(static_cast<unsigned char>(keyEnd[-1]))) {
      *--keyEnd = '\0';
    }

    size_t index;
    if (strcmp(key, "R") == 0) {
      index = 0;
    } else if (strcmp(key, "P") == 0) {
      index = 1;
    } else if (strcmp(key, "Y") == 0) {
      index = 2;
    } else if (strcmp(key, "Alt") == 0) {
      index = 3;
    } else {
      return false;
    }
    if (seen[index]) {
      return false;
    }

    char* value = separator + 1;
    while (isspace(static_cast<unsigned char>(*value))) {
      ++value;
    }
    char* end = nullptr;
    const float parsedValue = strtof(value, &end);
    while (end != nullptr && isspace(static_cast<unsigned char>(*end))) {
      ++end;
    }
    if (end == value || end == nullptr || *end != '\0' || !isfinite(parsedValue)) {
      return false;
    }

    seen[index] = true;
    ++fieldCount;
    if (next == nullptr) {
      break;
    }
    field = next + 1;
    if (*field == '\0') {
      return false;
    }
  }

  return fieldCount == 4 && seen[0] && seen[1] && seen[2] && seen[3];
}

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

const char* contentTypeFor(const String& path) {
  const int separator = path.lastIndexOf('.');
  if (separator < 0) {
    return "application/octet-stream";
  }
  String extension = path.substring(separator + 1);
  extension.toLowerCase();
  if (extension == "html") return "text/html; charset=utf-8";
  if (extension == "js" || extension == "mjs") return "text/javascript; charset=utf-8";
  if (extension == "css") return "text/css; charset=utf-8";
  if (extension == "json" || extension == "map") return "application/json";
  if (extension == "svg") return "image/svg+xml";
  if (extension == "png") return "image/png";
  if (extension == "jpg" || extension == "jpeg") return "image/jpeg";
  if (extension == "gif") return "image/gif";
  if (extension == "ico") return "image/x-icon";
  if (extension == "webp") return "image/webp";
  if (extension == "woff") return "font/woff";
  if (extension == "woff2") return "font/woff2";
  if (extension == "ttf") return "font/ttf";
  if (extension == "wasm") return "application/wasm";
  if (extension == "txt") return "text/plain; charset=utf-8";
  if (extension == "xml") return "application/xml";
  return "application/octet-stream";
}

bool serveFile(const String& path) {
  File file = SPIFFS.open(path, FILE_READ);
  if (!file || file.isDirectory()) {
    if (file) {
      file.close();
    }
    return false;
  }

  webServer.sendHeader("Cache-Control", path.startsWith("/assets/")
    ? "public, max-age=31536000, immutable"
    : path == "/index.html" ? "no-cache" : "public, max-age=3600");
  webServer.sendHeader("X-Content-Type-Options", "nosniff");
  webServer.streamFile(file, contentTypeFor(path));
  file.close();
  return true;
}

void handleNotFound() {
  if (webServer.method() != HTTP_GET) {
    webServer.send(405, "text/plain; charset=utf-8", "Method not allowed");
    return;
  }
  if (!filesystemReady) {
    webServer.send(503, "text/plain; charset=utf-8", "SPIFFS is unavailable");
    return;
  }

  const String path = webServer.uri();
  if (path.indexOf("..") >= 0 || path.indexOf('\\') >= 0) {
    webServer.send(404, "text/plain; charset=utf-8", "Not found");
    return;
  }
  if (serveFile(path)) {
    return;
  }

  const int lastSlash = path.lastIndexOf('/');
  const int lastDot = path.lastIndexOf('.');
  if (path.startsWith("/assets/") || lastDot > lastSlash) {
    webServer.send(404, "text/plain; charset=utf-8", "Not found");
    return;
  }
  // Only extensionless routes use the SPA entry point; missing assets stay 404s.
  if (!serveFile("/index.html")) {
    webServer.send(404, "text/plain; charset=utf-8", "Dashboard files are missing");
  }
}

void handleStatus() {
  String response;
  response.reserve(latestData.length() + lastFrame.length() + 220);
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
  response += ",\"last_packet_valid\":";
  response += hasLastFrame ? (lastFrameValid ? "true" : "false") : "null";
  response += ",\"last_frame\":\"";
  response += jsonEscape(lastFrame);
  response += "\",\"wifi_clients\":";
  response += String(WiFi.softAPgetStationNum());
  response += ",\"ssid\":\"";
  response += jsonEscape(WIFI_SSID);
  response += "\",\"ip\":\"";
  response += WiFi.softAPIP().toString();
  response += "\"}";
  webServer.sendHeader("Cache-Control", "no-store");
  webServer.send(200, "application/json; charset=utf-8", response);
}

void receiveStm32Telemetry() {
  while (Serial2.available() > 0) {
    const char c = static_cast<char>(Serial2.read());
    if (c == '\n') {
      if (currentLineOverflowed) {
        lastFrame = "Oversized UART frame";
        hasLastFrame = true;
        lastFrameValid = false;
        Serial.println("Rejected oversized UART telemetry frame.");
      } else {
        currentLine.trim();
        if (currentLine.length() > 0) {
          lastFrame = currentLine;
          hasLastFrame = true;
          lastFrameValid = parseTelemetryFrame(currentLine);
          if (lastFrameValid) {
            latestData = currentLine;
            hasTelemetry = true;
            lastTelemetryAt = millis();
            webSocket.broadcastTXT(currentLine);
          } else {
            Serial.print("Rejected invalid UART telemetry frame: ");
            Serial.println(currentLine);
          }
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

void webSocketEvent(uint8_t client, WStype_t type, uint8_t* payload, size_t length) {
  (void)payload;
  (void)length;
  if (type == WStype_CONNECTED) {
    Serial.printf("WebSocket client %u connected.\n", client);
  } else if (type == WStype_DISCONNECTED) {
    Serial.printf("WebSocket client %u disconnected.\n", client);
  }
}

void startAccessPoint() {
  if (!WiFi.mode(WIFI_AP) || !WiFi.softAP(WIFI_SSID, WIFI_PASSWORD)) {
    accessPointReady = false;
    nextAccessPointRetryAt = millis() + 5000;
    Serial.println("ERROR: Failed to start Wi-Fi hotspot; retrying in 5 seconds.");
    return;
  }

  accessPointReady = true;
  Serial.print("Wi-Fi name: ");
  Serial.println(WIFI_SSID);
  Serial.print("Dashboard: http://");
  Serial.println(WiFi.softAPIP());
  Serial.print("WebSocket telemetry endpoint: ws://");
  Serial.print(WiFi.softAPIP());
  Serial.print(":");
  Serial.println(WEBSOCKET_PORT);
}

void setup() {
  Serial.begin(9600);
  Serial2.setRxBufferSize(4096);
  Serial2.begin(UART_BAUD_RATE, SERIAL_8N1, STM32_RX_PIN, STM32_TX_PIN);
  currentLine.reserve(MAX_TELEMETRY_LINE_LENGTH + 1);
  latestData.reserve(MAX_TELEMETRY_LINE_LENGTH + 1);
  lastFrame.reserve(MAX_TELEMETRY_LINE_LENGTH + 1);

  Serial.println();
  Serial.println("ESP32 UART, WebSocket, and dashboard bridge starting...");

  // Do not format on mount failure; retain existing files and keep telemetry services up.
  filesystemReady = SPIFFS.begin(false);
  if (filesystemReady) {
    Serial.printf("SPIFFS usage: %u / %u bytes.\n",
      static_cast<unsigned int>(SPIFFS.usedBytes()),
      static_cast<unsigned int>(SPIFFS.totalBytes()));
  } else {
    Serial.println("ERROR: SPIFFS mount failed; the dashboard will be unavailable.");
  }
  startAccessPoint();
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  webServer.on("/status", HTTP_GET, handleStatus);
  webServer.onNotFound(handleNotFound);
  webServer.begin();
  Serial.println("HTTP and WebSocket services started.");
}

void loop() {
  receiveStm32Telemetry();
  webSocket.loop();
  webServer.handleClient();

  if (!accessPointReady &&
      static_cast<int32_t>(millis() - nextAccessPointRetryAt) >= 0) {
    startAccessPoint();
  }
}
