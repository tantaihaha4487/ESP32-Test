#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <Preferences.h>

// Create AsyncWebServer object on port 80
AsyncWebServer server(80);
Preferences preferences;

// Built-in LED pin (usually 2 for ESP32 DevKit V1)
const int LED_PIN = 2;

// Variables to store WiFi credentials
String ssid = "";
String password = "";

// Function to get scan results as JSON
String getScanResults() {
    int n = WiFi.scanNetworks();
    DynamicJsonDocument doc(1024);
    JsonArray array = doc.to<JsonArray>();

    for (int i = 0; i < n; ++i) {
        JsonObject network = array.createNestedObject();
        network["ssid"] = WiFi.SSID(i);
        network["rssi"] = WiFi.RSSI(i);
        network["encryption"] = WiFi.encryptionType(i);
    }
    
    String output;
    serializeJson(doc, output);
    return output;
}

void setup() {
    // Serial port for debugging purposes
    Serial.begin(115200);
    
    // Initialize LittleFS
    if(!LittleFS.begin(true)){
        Serial.println("An Error has occurred while mounting LittleFS");
        return;
    }

    // Initialize LED
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    // Initialize Preferences
    preferences.begin("wifi-conf", false);

    // Set WiFi to AP+STA mode
    WiFi.mode(WIFI_AP_STA);
    
    // Configure Access Point
    Serial.println("Setting up Access Point...");
    WiFi.softAP("ESP32-Setup", NULL); // Open network
    
    IPAddress IP = WiFi.softAPIP();
    Serial.print("AP IP address: ");
    Serial.println(IP);

    // Load saved credentials
    ssid = preferences.getString("ssid", "");
    password = preferences.getString("password", "");

    if (ssid != "") {
        Serial.println("Found saved credentials. Connecting to: " + ssid);
        WiFi.begin(ssid.c_str(), password.c_str());
    }

    // Route for root /
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        if (WiFi.status() == WL_CONNECTED) {
            request->send(LittleFS, "/index.html", "text/html");
        } else {
            request->send(LittleFS, "/wifi.html", "text/html");
        }
    });

    // Serve static files
    server.serveStatic("/style.css", LittleFS, "/style.css");

    // Scan endpoint
    server.on("/scan", HTTP_GET, [](AsyncWebServerRequest *request){
        int n = WiFi.scanComplete();
        if(n == -2){
            WiFi.scanNetworks(true);
            request->send(200, "application/json", "{\"status\":\"scanning\"}");
        } else if(n == -1){
            request->send(200, "application/json", "{\"status\":\"scanning\"}");
        } else {
            DynamicJsonDocument doc(2048);
            doc["status"] = "done";
            JsonArray array = doc.createNestedArray("networks");
            for (int i = 0; i < n; ++i) {
                JsonObject network = array.createNestedObject();
                network["ssid"] = WiFi.SSID(i);
                network["rssi"] = WiFi.RSSI(i);
                network["encryption"] = WiFi.encryptionType(i);
            }
            WiFi.scanDelete();
            String json;
            serializeJson(doc, json);
            request->send(200, "application/json", json);
        }
    });

    // Connect endpoint
    server.on("/connect", HTTP_POST, [](AsyncWebServerRequest *request){
        String inputSSID;
        String inputPassword;

        if (request->hasParam("ssid", true)) {
            inputSSID = request->getParam("ssid", true)->value();
        }
        if (request->hasParam("password", true)) {
            inputPassword = request->getParam("password", true)->value();
        }

        Serial.println("Connecting to: " + inputSSID);
        
        // Save credentials
        preferences.putString("ssid", inputSSID);
        preferences.putString("password", inputPassword);
        
        WiFi.begin(inputSSID.c_str(), inputPassword.c_str());
        request->send(200, "application/json", "{\"status\":\"attempting\"}");
    });

    // LED Status endpoint
    server.on("/status", HTTP_GET, [](AsyncWebServerRequest *request){
        DynamicJsonDocument doc(200);
        doc["led"] = (digitalRead(LED_PIN) == HIGH);
        doc["wifi_status"] = (int)WiFi.status();
        if (WiFi.status() == WL_CONNECTED) {
            doc["ip"] = WiFi.localIP().toString();
        }
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });

    // LED Toggle endpoint
    server.on("/toggle", HTTP_GET, [](AsyncWebServerRequest *request){
        String state;
        if (request->hasParam("state")) {
            state = request->getParam("state")->value();
            if (state == "on") {
                digitalWrite(LED_PIN, HIGH);
            } else {
                digitalWrite(LED_PIN, LOW);
            }
        }
        
        DynamicJsonDocument doc(200);
        doc["led"] = (digitalRead(LED_PIN) == HIGH);
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });

    // Start server
    server.begin();
}

void loop() {
    // Nothing needed here for AsyncWebServer
    delay(1000);
}
