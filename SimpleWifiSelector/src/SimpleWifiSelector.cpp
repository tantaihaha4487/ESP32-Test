/*
  esp32_wifi_manager_safe_v2_spiffs.ino
  Single-file WiFi manager for ESP32 with files served from SPIFFS.

  - Place index.html, style.css, app.js (and any assets) in sketch_folder/data/
  - Upload using "ESP32 Sketch Data Upload" (Arduino IDE) or `pio run --target uploadfs` (PlatformIO)
  - This file keeps AP+STA behavior and original endpoints (/scan, /connect, /led, /status, /scan_trigger)
*/
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <SPIFFS.h>

Preferences preferences;
WebServer server(80);

// Preferences keys
const char* PREF_NS = "wifi_mgr";
const char* PREF_SSID = "ssid";
const char* PREF_PASS = "pass";

// AP settings
const char* AP_SSID = "ESP32_Config";
const char* AP_PASS = "configureme";  // set to "" for open AP

// Timing & state
unsigned long connectTimeoutMs = 15000;
const int LED_PIN = LED_BUILTIN;  // adjust if needed

// State flags
String savedSsid = "";
String savedPass = "";
bool haveSaved = false;

volatile bool scanRequested = false;
volatile bool scanning = false;
String scanResultsJson = "[]";  // last completed scan results JSON

// ---------- forward declarations ----------
void handleRoot();
void handleScan();
void handleScanTrigger();
void handleStatus();
void handleConnect();
void handleLed();
void handleNotFound();
bool handleFileRead(const String& path);

// WiFi event IDs
void onWiFiEvent(WiFiEvent_t event) {
  switch (event) {
    case WIFI_EVENT_STA_START:
      Serial.println("Event: STA_START");
      break;
    case WIFI_EVENT_STA_CONNECTED:
      Serial.println("Event: STA_CONNECTED");
      break;
    case IP_EVENT_STA_GOT_IP:
      Serial.printf("Event: GOT_IP: %s\n", WiFi.localIP().toString().c_str());
      break;
    case WIFI_EVENT_STA_DISCONNECTED:
      // Serial.printf("Event: STA_DISCONNECTED, reason=%d\n", WiFi.reason());
      break;
    case WIFI_EVENT_AP_START:
      Serial.println("Event: AP_START");
      break;
    default:
      break;
  }
}

// ---------- helper utilities ----------
void savePrefs(const char* ssid, const char* pass) {
  preferences.begin(PREF_NS, false);
  preferences.putString(PREF_SSID, ssid);
  preferences.putString(PREF_PASS, pass);
  preferences.end();
}

void loadPrefs() {
  preferences.begin(PREF_NS, true);
  savedSsid = preferences.getString(PREF_SSID, "");
  savedPass = preferences.getString(PREF_PASS, "");
  haveSaved = savedSsid.length() > 0;
  preferences.end();
}

String escapeJson(const String& s) {
  String r;
  for (size_t i = 0; i < s.length(); ++i) {
    char c = s[i];
    if (c == '"') r += "\\\"";
    else if (c == '\n') r += "\\n";
    else r += c;
  }
  return r;
}

// ---------- HTTP handlers ----------

// Serve index.html for root
void handleRoot() {
  if (WiFi.status() == WL_CONNECTED) {
    // Even when connected we still may want to serve the control UI file from SPIFFS
    // index.html will include logic to call /status and /led
  }
  if (!handleFileRead("/index.html")) {
    server.send(500, "text/plain", "index.html not found");
  }
}

// Return last completed scan results as JSON array
void handleScan() {
  if (scanning) {
    server.send(200, "application/json", "[{\"_scanning\":true}]");
    return;
  }
  server.send(200, "application/json", scanResultsJson);
}

// Trigger a new async scan (returns started: true/false)
void handleScanTrigger() {
  bool started = false;
  if (!scanning) {
    int r = WiFi.scanNetworks(true);  // async
    if (r >= 0) {
      scanning = true;
      started = true;
      Serial.println("handleScanTrigger: Async scan started.");
    } else {
      Serial.printf("handleScanTrigger: scan start failed (code %d)\n", r);
    }
  }
  String out = "{\"started\":" + String(started ? "true" : "false") + "}";
  server.send(200, "application/json", out);
}

// Status endpoint: connected flag, ssid, ip
void handleStatus() {
  String out = "{";
  out += "\"connected\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false");
  out += ",\"ssid\":\"" + escapeJson(WiFi.SSID()) + "\"";
  out += ",\"ip\":\"" + WiFi.localIP().toString() + "\"}";
  server.send(200, "application/json", out);
}

// Save credentials and start immediate connect attempt (non blocking here)
void handleConnect() {
  if (server.method() != HTTP_POST) {
    server.send(405, "text/plain", "Method Not Allowed");
    return;
  }
  String body = server.arg("plain");
  String ssid = "", pass = "";
  int i_ssid = body.indexOf("\"ssid\"");
  if (i_ssid >= 0) {
    int c = body.indexOf(':', i_ssid);
    int q1 = body.indexOf('"', c);
    int q2 = body.indexOf('"', q1 + 1);
    if (q1 >= 0 && q2 > q1) ssid = body.substring(q1 + 1, q2);
  }
  int i_pass = body.indexOf("\"pass\"");
  if (i_pass >= 0) {
    int c = body.indexOf(':', i_pass);
    int q1 = body.indexOf('"', c);
    int q2 = body.indexOf('"', q1 + 1);
    if (q1 >= 0 && q2 > q1) pass = body.substring(q1 + 1, q2);
  }

  if (ssid.length() == 0) {
    server.send(400, "application/json", "{\"success\":false,\"message\":\"SSID required\"}");
    return;
  }

  // Save credentials immediately to preferences
  savePrefs(ssid.c_str(), pass.c_str());
  Serial.printf("handleConnect: saved credentials for '%s'\n", ssid.c_str());

  // Respond success and include current IP if connected
  String resp = "{";
  resp += "\"success\":true,";
  resp += "\"ip\":\"" + WiFi.localIP().toString() + "\"";
  resp += "}";
  server.send(200, "application/json", resp);

  // Start an immediate connect attempt from loop() by updating savedSsid/savedPass and haveSaved
  savedSsid = ssid;
  savedPass = pass;
  haveSaved = true;
  // do not stop AP here; AP remains active to avoid crashing Wi-Fi internals
}

// LED control endpoint
void handleLed() {
  String state = server.arg("state");
  if (state.length()) {
    if (state == "on") digitalWrite(LED_PIN, HIGH);
    else if (state == "off") digitalWrite(LED_PIN, LOW);
  }
  bool isOn = digitalRead(LED_PIN) == HIGH;
  String out = String("{\"on\":") + (isOn ? "true" : "false") + "}";
  server.send(200, "application/json", out);
}

void handleNotFound() {
  String uri = server.uri();
  if (handleFileRead(uri)) return;  // served from SPIFFS
  server.send(404, "text/plain", "Not found");
}

// Serve files from SPIFFS. Returns true if file found and served.
bool handleFileRead(const String& path) {
  String filePath = path;
  if (filePath == "/") filePath = "/index.html";
  if (SPIFFS.exists(filePath)) {
    File f = SPIFFS.open(filePath, "r");
    if (!f) {
      Serial.printf("handleFileRead: failed to open %s\n", filePath.c_str());
      f.close();
      return false;
    }
    String contentType = "text/plain";
    if (filePath.endsWith(".htm") || filePath.endsWith(".html")) contentType = "text/html";
    else if (filePath.endsWith(".css")) contentType = "text/css";
    else if (filePath.endsWith(".js")) contentType = "application/javascript";
    else if (filePath.endsWith(".png")) contentType = "image/png";
    else if (filePath.endsWith(".jpg") || filePath.endsWith(".jpeg")) contentType = "image/jpeg";
    else if (filePath.endsWith(".svg")) contentType = "image/svg+xml";
    else if (filePath.endsWith(".json")) contentType = "application/json";

    server.streamFile(f, contentType);
    return true;
  }
  return false;
}

// ---------- startup & main loop ----------
unsigned long lastScanCheck = 0;
unsigned long lastConnectTry = 0;
const unsigned long scanCheckInterval = 300;       // ms check for scan completion
const unsigned long connectRetryInterval = 10000;  // ms between connect tries

// Non-blocking connect helper (uses blocking WiFi.begin for up to timeoutMs)
bool tryConnectOnceBlocking(const String& ssid, const String& pass, unsigned long timeoutMs) {
  if (ssid.length() == 0) return false;
  Serial.printf("tryConnectOnceBlocking: starting connect to '%s' (timeout %lu ms)\n", ssid.c_str(), timeoutMs);
  WiFi.mode(WIFI_STA);  // ensure STA works (AP remains, but this sets mode)
  WiFi.begin(ssid.c_str(), pass.c_str());
  unsigned long start = millis();
  while (millis() - start < timeoutMs) {
    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf("tryConnectOnceBlocking: connected - IP: %s\n", WiFi.localIP().toString().c_str());
      return true;
    }
    delay(200);
  }
  Serial.println("tryConnectOnceBlocking: connect timed out");
  return false;
}

void setup() {
  Serial.begin(115200);
  delay(50);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  Serial.println("\n\nESP32 WiFi Manager SAFE v2 (SPIFFS) starting...");

  // Mount SPIFFS
  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS mount failed!");
  } else {
    Serial.println("SPIFFS mounted.");
  }

  // Load saved credentials
  loadPrefs();
  if (haveSaved) Serial.printf("Loaded saved SSID: '%s'\n", savedSsid.c_str());
  else Serial.println("No saved WiFi credentials.");

  // Start AP+STA mode safely and keep AP running always (do NOT call softAPdisconnect anywhere)
  WiFi.mode(WIFI_AP_STA);
  if (strlen(AP_PASS) >= 8) {
    WiFi.softAP(AP_SSID, AP_PASS);
    Serial.printf("AP started (secured): %s\n", AP_SSID);
  } else {
    WiFi.softAP(AP_SSID);
    Serial.printf("AP started (open): %s\n", AP_SSID);
  }
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());

  // Register WiFi event callback for debug
  WiFi.onEvent(onWiFiEvent);

  // Register HTTP routes
  server.on("/", HTTP_GET, handleRoot);
  server.on("/scan", HTTP_GET, handleScan);
  server.on("/scan_trigger", HTTP_POST, handleScanTrigger);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/connect", HTTP_POST, handleConnect);
  server.on("/led", HTTP_GET, handleLed);
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println("HTTP server started.");

  // (Optional) show SPIFFS listing for debug
  Serial.println("SPIFFS files:");
  File root = SPIFFS.open("/", "r");
  File file = root.openNextFile();
  while (file) {
    Serial.printf(" - %s (%u bytes)\n", file.name(), (unsigned)file.size());
    file = root.openNextFile();
  }
}

void loop() {
  server.handleClient();

  // Check if an async scan is in progress (WiFi.scanComplete returns -2 if still scanning)
  if (scanning && (millis() - lastScanCheck) > scanCheckInterval) {
    lastScanCheck = millis();
    int n = WiFi.scanComplete();  // -2: still scanning, -1: failed/no scan, >=0: number networks
    if (n == -2) {
      // still scanning
    } else if (n == -1) {
      Serial.println("loop: scanComplete returned -1 (no scan or error).");
      scanning = false;
      scanResultsJson = "[]";
    } else {
      // Got results
      Serial.printf("loop: scanComplete: %d networks\n", n);
      String out = "[";
      for (int i = 0; i < n; i++) {
        String ssid = WiFi.SSID(i);
        int rssi = WiFi.RSSI(i);
        bool secure = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
        out += "{";
        out += "\"ssid\":\"" + escapeJson(ssid) + "\"";
        out += ",\"rssi\":" + String(rssi);
        out += ",\"secure\":" + String(secure ? "true" : "false");
        out += "}";
        if (i < n - 1) out += ",";
      }
      out += "]";
      scanResultsJson = out;
      WiFi.scanDelete();  // free scan results memory
      scanning = false;
      Serial.println("loop: scan results ready.");
    }
  }

  // Attempt connect if we have saved credentials and not connected. Do this periodically.
  if (haveSaved && WiFi.status() != WL_CONNECTED && (millis() - lastConnectTry > connectRetryInterval)) {
    lastConnectTry = millis();
    Serial.printf("loop: attempt to connect to saved SSID '%s'\n", savedSsid.c_str());
    bool ok = tryConnectOnceBlocking(savedSsid, savedPass, connectTimeoutMs);
    if (ok) {
      Serial.printf("loop: connected OK. STA IP = %s\n", WiFi.localIP().toString().c_str());
      // Do NOT stop AP. Keep AP to avoid crashes.
    } else {
      Serial.println("loop: connect attempt failed. Will retry later.");
    }
  }

  delay(10);
}
