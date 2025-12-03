/*
  esp32_wifi_manager.ino
  Single-file WiFi manager for ESP32
  - Tries saved credentials on boot
  - Falls back to AP + web portal if can't connect
  - Web portal: list scanned networks or manual config
  - Saves credentials in Preferences on successful connect

  Tested conceptually with ESP32 Arduino core (WiFi.h + WebServer.h).
*/

#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>

Preferences preferences;
WebServer server(80);

const char* pref_namespace = "wifi_mgr";
const char* pref_ssid_key = "ssid";
const char* pref_pass_key = "pass";

const char* ap_ssid = "ESP32_Config";
const char* ap_password = "configureme"; // optional, 8+ chars recommended

unsigned long wifi_connect_deadline = 10000; // ms to wait for stored credentials connect
unsigned long portal_timeout = 5 * 60 * 1000UL; // keep portal for 5 minutes before reboot

String savedSsid = "";
String savedPass = "";

unsigned long portalStartMillis = 0;
bool portalActive = false;

//==================== HTML ====================
// Note: keep simple for single-file. JS fetches /scan for AP mode.
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!doctype html>
<html>
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>ESP32 WiFi Config</title>
  <style>
    body{font-family:Arial,Helvetica,sans-serif;margin:16px}
    .net{padding:8px;border:1px solid #ddd;margin:6px 0;display:flex;justify-content:space-between;align-items:center}
    .net .left{display:flex;flex-direction:column}
    .btn{padding:6px 10px;border-radius:6px;border:0;background:#1E90FF;color:#fff;cursor:pointer}
    .btn.secondary{background:#4CAF50}
    .small{font-size:0.9em;color:#555}
    input{padding:8px;margin:6px 0;width:100%}
    form{max-width:420px}
    #status{margin-top:12px}
  </style>
</head>
<body>
  <h2>ESP32 WiFi Configuration</h2>
  <div id="status" class="small">Loading...</div>

  <h3>Available Networks</h3>
  <div id="networks">Scanning...</div>
  <p><button id="rescan" class="btn">Rescan</button></p>

  <h3>Manual / Selected</h3>
  <form id="cfg">
    <label>SSID</label>
    <input id="ssid" name="ssid" placeholder="SSID" required />
    <label>Password</label>
    <input id="pass" name="pass" placeholder="Password" />
    <p>
      <button type="submit" class="btn secondary">Connect & Save</button>
    </p>
  </form>

<script>
async function fetchJSON(url){
  const res = await fetch(url);
  return res.json();
}

function setStatus(t){ document.getElementById('status').textContent = t; }

async function scan(){
  setStatus("Scanning for networks...");
  try{
    const data = await fetchJSON('/scan');
    const list = document.getElementById('networks');
    list.innerHTML = '';
    if(!data || data.length===0){ list.textContent = 'No networks found.'; setStatus('AP mode: select or enter network'); return; }
    data.forEach(net=>{
      const div = document.createElement('div');
      div.className = 'net';
      const left = document.createElement('div'); left.className='left';
      left.innerHTML = `<strong>${net.ssid}</strong><span class="small">rssi:${net.rssi} ${net.secure? '• secured' : '• open'}</span>`;
      const btn = document.createElement('button');
      btn.className = 'btn';
      btn.textContent = 'Use';
      btn.onclick = ()=>{ document.getElementById('ssid').value = net.ssid; document.getElementById('pass').focus(); };
      div.appendChild(left); div.appendChild(btn);
      list.appendChild(div);
    });
    setStatus('AP mode: select or enter network');
  } catch(e){
    setStatus('Scan failed: ' + e);
  }
}

document.getElementById('rescan').addEventListener('click', scan);

document.getElementById('cfg').addEventListener('submit', async (ev)=>{
  ev.preventDefault();
  const ssid = document.getElementById('ssid').value.trim();
  const pass = document.getElementById('pass').value;
  if(!ssid){ setStatus('SSID required'); return; }
  setStatus('Trying to connect...');
  try{
    const resp = await fetch('/connect', {
      method:'POST',
      headers:{'Content-Type':'application/json'},
      body: JSON.stringify({ssid, pass})
    });
    const j = await resp.json();
    if(j.success){
      setStatus('Connected and saved! IP: ' + j.ip);
      // Optionally show instructions to disconnect from AP and join network
    } else {
      setStatus('Connect failed: ' + j.message);
    }
  } catch(e){ setStatus('Request failed: ' + e); }
});

window.onload = ()=>{ scan(); fetch('/status').then(r=>r.json()).then(s=>{
  if(s.connected) setStatus('Already connected: ' + s.ssid + ' — IP: ' + s.ip);
}); }
</script>
</body>
</html>
)rawliteral";
//==================== end HTML ====================

String pageFromProgmem(){
  return String(INDEX_HTML);
}

void saveCredentials(const char* ssid, const char* pass){
  preferences.begin(pref_namespace, false);
  preferences.putString(pref_ssid_key, ssid);
  preferences.putString(pref_pass_key, pass);
  preferences.end();
}

void clearCredentials(){
  preferences.begin(pref_namespace, false);
  preferences.remove(pref_ssid_key);
  preferences.remove(pref_pass_key);
  preferences.end();
}

void loadSavedCredentials(){
  preferences.begin(pref_namespace, true);
  savedSsid = preferences.getString(pref_ssid_key, "");
  savedPass = preferences.getString(pref_pass_key, "");
  preferences.end();
}

bool tryConnectWithSaved(unsigned long timeoutMs){
  if(savedSsid.length() == 0) return false;
  WiFi.mode(WIFI_STA);
  WiFi.begin(savedSsid.c_str(), savedPass.c_str());
  unsigned long start = millis();
  while(millis() - start < timeoutMs){
    if(WiFi.status() == WL_CONNECTED){
      Serial.printf("Connected to saved SSID %s, IP: %s\n", savedSsid.c_str(), WiFi.localIP().toString().c_str());
      return true;
    }
    delay(250);
  }
  Serial.println("Saved credentials didn't connect in time.");
  return false;
}

String jsonEscape(const String &s){
  String r;
  for(char c: s){
    if(c == '"') r += "\\\"";
    else if(c == '\n') r += "\\n";
    else r += c;
  }
  return r;
}

void handleRoot(){
  server.send(200, "text/html", pageFromProgmem());
}

void handleScan(){
  // Perform scan, return JSON array of {ssid, rssi, secure}
  int n = WiFi.scanNetworks();
  String out = "[";
  for(int i=0;i<n;i++){
    String ssid = WiFi.SSID(i);
    int rssi = WiFi.RSSI(i);
    bool secure = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
    out += "{";
    out += "\"ssid\":\"" + jsonEscape(ssid) + "\"";
    out += ",\"rssi\":" + String(rssi);
    out += ",\"secure\":" + String(secure ? "true":"false");
    out += "}";
    if(i < n-1) out += ",";
  }
  out += "]";
  server.send(200, "application/json", out);
}

void handleStatus(){
  String out;
  out += "{";
  out += "\"connected\":" + String(WiFi.status() == WL_CONNECTED ? "true":"false");
  out += ",\"ssid\":\"" + jsonEscape(WiFi.SSID()) + "\"";
  out += ",\"ip\":\"" + WiFi.localIP().toString() + "\"";
  out += "}";
  server.send(200, "application/json", out);
}

void handleConnect(){
  if(server.method() != HTTP_POST){
    server.send(405, "text/plain", "Method Not Allowed");
    return;
  }
  String body = server.arg("plain");
  // Expect JSON {"ssid":"...", "pass":"..."}
  String ssid="", pass="";
  // Very small lightweight JSON parse (not robust but fine for simple inputs)
  int i_ssid = body.indexOf("\"ssid\"");
  if(i_ssid >= 0){
    int c = body.indexOf(':', i_ssid);
    int q1 = body.indexOf('"', c);
    int q2 = body.indexOf('"', q1+1);
    if(q1>=0 && q2>q1) ssid = body.substring(q1+1, q2);
  }
  int i_pass = body.indexOf("\"pass\"");
  if(i_pass >= 0){
    int c = body.indexOf(':', i_pass);
    int q1 = body.indexOf('"', c);
    int q2 = body.indexOf('"', q1+1);
    if(q1>=0 && q2>q1) pass = body.substring(q1+1, q2);
  }

  if(ssid.length() == 0){
    server.send(400, "application/json", "{\"success\":false,\"message\":\"SSID required\"}");
    return;
  }

  // Try connect
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), pass.c_str());
  unsigned long start = millis();
  bool connected = false;
  while(millis() - start < wifi_connect_deadline){
    if(WiFi.status() == WL_CONNECTED){
      connected = true;
      break;
    }
    delay(250);
  }
  if(connected){
    saveCredentials(ssid.c_str(), pass.c_str());
    Serial.printf("Saved new credentials for %s. IP: %s\n", ssid.c_str(), WiFi.localIP().toString().c_str());
    String resp = "{";
    resp += "\"success\":true,";
    resp += "\"ip\":\"" + WiFi.localIP().toString() + "\"";
    resp += "}";
    server.send(200, "application/json", resp);
    // Optionally stop portal soon
    portalActive = false;
  } else {
    String msg = "Connection timed out";
    Serial.printf("Failed to connect to %s\n", ssid.c_str());
    server.send(200, "application/json", "{\"success\":false,\"message\":\"" + msg + "\"}");
  }
}

void handleNotFound(){
  if (server.uri() == "/") handleRoot();
  else server.send(404, "text/plain", "Not found");
}

void startPortal(){
  Serial.println("Starting AP portal...");
  portalStartMillis = millis();
  portalActive = true;
  WiFi.mode(WIFI_AP_STA); // allow scanning + AP
  bool apSec = strlen(ap_password) >= 8;
  if(apSec){
    WiFi.softAP(ap_ssid, ap_password);
  } else {
    WiFi.softAP(ap_ssid);
  }
  IPAddress myIP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(myIP);

  // Setup server routes
  server.on("/", HTTP_GET, handleRoot);
  server.on("/scan", HTTP_GET, handleScan);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/connect", HTTP_POST, handleConnect);
  server.onNotFound(handleNotFound);

  server.begin();
  Serial.println("HTTP server started");
}

void stopPortal(){
  Serial.println("Stopping portal...");
  server.stop();
  WiFi.softAPdisconnect(true);
  portalActive = false;
}

void setup(){
  Serial.begin(115200);
  delay(100);
  Serial.println("\n\nESP32 WiFi Manager starting...");

  loadSavedCredentials();
  Serial.printf("Saved SSID: '%s'\n", savedSsid.c_str());

  bool ok = false;
  if(savedSsid.length() > 0){
    Serial.println("Attempting to connect with saved credentials...");
    ok = tryConnectWithSaved(wifi_connect_deadline);
  }

  if(ok){
    Serial.println("Connected using saved credentials.");
    // Optionally start minimal web server for status only (not portal)
    server.on("/status", HTTP_GET, handleStatus);
    server.on("/", HTTP_GET, [](){
      server.send(200, "text/plain", "ESP32 is connected to WiFi. IP: " + WiFi.localIP().toString());
    });
    server.begin();
  } else {
    // Start AP portal
    startPortal();
  }
}

void loop(){
  // Handle web server if running
  if(portalActive || server.client()){
    server.handleClient();
  }

  // If portal active, enforce timeout
  if(portalActive){
    if(millis() - portalStartMillis > portal_timeout){
      Serial.println("Portal timeout reached, rebooting...");
      delay(200);
      ESP.restart();
    }
  } else {
    // If not portal and WiFi disconnected, try to reconnect or start portal
    if(WiFi.status() != WL_CONNECTED){
      static unsigned long lastAttempt = 0;
      if(millis() - lastAttempt > 5000){
        lastAttempt = millis();
        Serial.println("Not connected. Trying saved credentials once...");
        loadSavedCredentials();
        if(savedSsid.length()>0){
          if(tryConnectWithSaved(5000)){
            Serial.println("Reconnected.");
          } else {
            Serial.println("Reconnect failed — starting portal.");
            startPortal();
          }
        } else {
          Serial.println("No saved credentials — starting portal.");
          startPortal();
        }
      }
    }
  }

  delay(10);
}
