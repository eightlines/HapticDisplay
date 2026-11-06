#include <Arduino.h>
#include <SimpleFOC.h>
#include <Wire.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include "wifi_secrets.h"  // Generated from .env by load_env.py

// ===== WiFi CONFIGURATION =====
// Access Point mode - ESP32 creates its own network
const char* AP_SSID = WIFI_AP_SSID;
const char* AP_PASSWORD = WIFI_AP_PASSWORD;  // Min 8 characters

// Or connect to existing WiFi (set USE_STATION_MODE to true)
#define USE_STATION_MODE true
const char* STA_SSID = WIFI_STA_SSID;
const char* STA_PASSWORD = WIFI_STA_PASSWORD;

// Web server on port 80
AsyncWebServer server(80);

// Motor driver pins
#define IN1 27
#define IN2 26
#define IN3 25
#define EN  33

// I2C for AS5600 sensor
// SDA → GPIO 22
// SCL → GPIO 21

// Driver
BLDCDriver3PWM driver = BLDCDriver3PWM(IN1, IN2, IN3);

// Motor - 7 pole pairs
BLDCMotor motor = BLDCMotor(7);

// AS5600 magnetic sensor
MagneticSensorI2C sensor = MagneticSensorI2C(AS5600_I2C);

// ===== HAPTIC DETENT CONFIGURATION =====
int numDetents = 12;            // Number of detents per rotation (changeable!)
float detentStrength = 1.5;    // Max voltage for detent force (0-5)
float deadZone = 0.03;         // No torque within this distance from detent (radians)

// Spring physics parameters
float springStiffness = 0.2;   // How stiff the spring feels (higher = snappier)
float damping = 0.02;          // Velocity damping (higher = less bouncy, more controlled)
float overshootAllow = 0.8;    // Allow detent to "stretch" past center (0-1, higher = more elastic)

// Current state (updated in loop)
volatile float currentAngle = 0;
volatile int currentDetent = 0;
volatile float currentVelocity = 0;

// ===== EMBEDDED HTML PAGE =====
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Haptic Knob Control</title>
  <style>
    * { box-sizing: border-box; margin: 0; padding: 0; }
    body {
      font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
      background: linear-gradient(135deg, #1a1a2e 0%, #16213e 100%);
      color: #eee;
      min-height: 100vh;
      padding: 20px;
    }
    .container { max-width: 600px; margin: 0 auto; }
    h1 {
      text-align: center;
      margin-bottom: 20px;
      color: #00d4ff;
      text-shadow: 0 0 20px rgba(0,212,255,0.5);
    }
    .card {
      background: rgba(255,255,255,0.1);
      border-radius: 15px;
      padding: 20px;
      margin-bottom: 20px;
      backdrop-filter: blur(10px);
      border: 1px solid rgba(255,255,255,0.1);
    }
    .card h2 {
      color: #00d4ff;
      margin-bottom: 15px;
      font-size: 1.2em;
      border-bottom: 1px solid rgba(0,212,255,0.3);
      padding-bottom: 10px;
    }
    .display-row {
      display: flex;
      justify-content: space-between;
      align-items: center;
      padding: 10px 0;
      border-bottom: 1px solid rgba(255,255,255,0.1);
    }
    .display-row:last-child { border-bottom: none; }
    .display-value {
      font-size: 1.5em;
      font-weight: bold;
      color: #00ff88;
      font-family: 'Courier New', monospace;
    }
    .knob-display {
      width: 150px;
      height: 150px;
      margin: 20px auto;
      position: relative;
    }
    .knob-circle {
      width: 100%;
      height: 100%;
      border-radius: 50%;
      background: conic-gradient(from 0deg, #00d4ff, #00ff88, #ff6b6b, #00d4ff);
      display: flex;
      align-items: center;
      justify-content: center;
    }
    .knob-inner {
      width: 80%;
      height: 80%;
      border-radius: 50%;
      background: #1a1a2e;
      display: flex;
      align-items: center;
      justify-content: center;
      flex-direction: column;
    }
    .knob-detent { font-size: 2em; font-weight: bold; color: #00d4ff; }
    .knob-total { font-size: 0.9em; color: #888; }
    .param-row {
      display: flex;
      flex-wrap: wrap;
      align-items: center;
      padding: 12px 0;
      border-bottom: 1px solid rgba(255,255,255,0.1);
      gap: 10px;
    }
    .param-row:last-child { border-bottom: none; }
    .param-label {
      flex: 1;
      min-width: 100px;
    }
    .param-label small { color: #888; font-size: 0.8em; display: block; }
    .param-input {
      width: 80px;
      padding: 8px;
      border: 1px solid rgba(0,212,255,0.5);
      border-radius: 8px;
      background: rgba(0,0,0,0.3);
      color: #fff;
      font-size: 1em;
      text-align: center;
    }
    .param-slider {
      flex: 2;
      min-width: 120px;
      -webkit-appearance: none;
      height: 8px;
      border-radius: 4px;
      background: rgba(255,255,255,0.2);
    }
    .param-slider::-webkit-slider-thumb {
      -webkit-appearance: none;
      width: 20px;
      height: 20px;
      border-radius: 50%;
      background: #00d4ff;
      cursor: pointer;
    }
    .btn {
      background: linear-gradient(135deg, #00d4ff, #00ff88);
      color: #1a1a2e;
      border: none;
      padding: 12px 30px;
      border-radius: 25px;
      font-size: 1em;
      font-weight: bold;
      cursor: pointer;
      width: 100%;
      margin-top: 15px;
      transition: transform 0.2s, box-shadow 0.2s;
    }
    .btn:hover {
      transform: translateY(-2px);
      box-shadow: 0 5px 20px rgba(0,212,255,0.4);
    }
    .btn:active { transform: translateY(0); }
    .status {
      text-align: center;
      padding: 10px;
      border-radius: 8px;
      margin-top: 10px;
      display: none;
    }
    .status.success { display: block; background: rgba(0,255,136,0.2); color: #00ff88; }
    .status.error { display: block; background: rgba(255,107,107,0.2); color: #ff6b6b; }
    .api-info {
      font-size: 0.85em;
      color: #888;
      margin-top: 10px;
    }
    .api-info code {
      background: rgba(0,0,0,0.3);
      padding: 2px 6px;
      border-radius: 4px;
      color: #00d4ff;
    }
  </style>
</head>
<body>
  <div class="container">
    <h1>🎛️ Haptic Knob</h1>
    
    <div class="card">
      <h2>📊 Live Data</h2>
      <div class="knob-display">
        <div class="knob-circle">
          <div class="knob-inner">
            <span class="knob-detent" id="detentNum">-</span>
            <span class="knob-total">of <span id="totalDetents">-</span></span>
          </div>
        </div>
      </div>
      <div class="display-row">
        <span>Angle (rad)</span>
        <span class="display-value" id="angleVal">0.00</span>
      </div>
      <div class="display-row">
        <span>Angle (deg)</span>
        <span class="display-value" id="angleDeg">0.0°</span>
      </div>
      <div class="display-row">
        <span>Velocity</span>
        <span class="display-value" id="velocityVal">0.00</span>
      </div>
    </div>
    
    <div class="card">
      <h2>⚙️ Parameters</h2>
      
      <div class="param-row">
        <div class="param-label">Detents<small>Number of positions</small></div>
        <input type="range" class="param-slider" id="detentsSlider" min="2" max="36" value="12">
        <input type="number" class="param-input" id="detents" min="2" max="36" value="12">
      </div>
      
      <div class="param-row">
        <div class="param-label">Strength<small>Detent force (0-5)</small></div>
        <input type="range" class="param-slider" id="strengthSlider" min="0" max="5" step="0.1" value="1.5">
        <input type="number" class="param-input" id="strength" min="0" max="5" step="0.1" value="1.5">
      </div>
      
      <div class="param-row">
        <div class="param-label">Dead Zone<small>Rest threshold (rad)</small></div>
        <input type="range" class="param-slider" id="deadZoneSlider" min="0" max="0.2" step="0.01" value="0.03">
        <input type="number" class="param-input" id="deadZone" min="0" max="0.2" step="0.01" value="0.03">
      </div>
      
      <div class="param-row">
        <div class="param-label">Stiffness<small>Spring firmness</small></div>
        <input type="range" class="param-slider" id="stiffnessSlider" min="0.1" max="3" step="0.1" value="0.2">
        <input type="number" class="param-input" id="stiffness" min="0.1" max="3" step="0.1" value="0.2">
      </div>
      
      <div class="param-row">
        <div class="param-label">Damping<small>Oscillation control</small></div>
        <input type="range" class="param-slider" id="dampingSlider" min="0" max="0.2" step="0.005" value="0.02">
        <input type="number" class="param-input" id="damping" min="0" max="0.2" step="0.005" value="0.02">
      </div>
      
      <div class="param-row">
        <div class="param-label">Overshoot<small>Elasticity (0-1)</small></div>
        <input type="range" class="param-slider" id="overshootSlider" min="0.1" max="1" step="0.05" value="0.8">
        <input type="number" class="param-input" id="overshoot" min="0.1" max="1" step="0.05" value="0.8">
      </div>
      
      <button class="btn" onclick="applySettings()">Apply Settings</button>
      <div class="status" id="status"></div>
      
      <div class="api-info">
        <strong>API Endpoints:</strong><br>
        <code>GET /api/data</code> - Current position data<br>
        <code>GET /api/params</code> - Current parameters<br>
        <code>POST /api/params</code> - Update parameters (JSON)
      </div>
    </div>
  </div>
  
  <script>
    // Sync sliders with inputs
    const params = ['detents', 'strength', 'deadZone', 'stiffness', 'damping', 'overshoot'];
    params.forEach(p => {
      const slider = document.getElementById(p + 'Slider');
      const input = document.getElementById(p);
      slider.oninput = () => input.value = slider.value;
      input.oninput = () => slider.value = input.value;
    });
    
    // Fetch live data
    async function fetchData() {
      try {
        const res = await fetch('/api/data');
        const data = await res.json();
        document.getElementById('angleVal').textContent = data.angle.toFixed(2);
        document.getElementById('angleDeg').textContent = (data.angle * 180 / Math.PI).toFixed(1) + '°';
        document.getElementById('velocityVal').textContent = data.velocity.toFixed(2);
        document.getElementById('detentNum').textContent = data.detent;
        document.getElementById('totalDetents').textContent = data.numDetents;
      } catch(e) { console.error('Fetch error:', e); }
    }
    
    // Fetch current params on load
    async function fetchParams() {
      try {
        const res = await fetch('/api/params');
        const p = await res.json();
        document.getElementById('detents').value = p.numDetents;
        document.getElementById('detentsSlider').value = p.numDetents;
        document.getElementById('strength').value = p.detentStrength;
        document.getElementById('strengthSlider').value = p.detentStrength;
        document.getElementById('deadZone').value = p.deadZone;
        document.getElementById('deadZoneSlider').value = p.deadZone;
        document.getElementById('stiffness').value = p.springStiffness;
        document.getElementById('stiffnessSlider').value = p.springStiffness;
        document.getElementById('damping').value = p.damping;
        document.getElementById('dampingSlider').value = p.damping;
        document.getElementById('overshoot').value = p.overshootAllow;
        document.getElementById('overshootSlider').value = p.overshootAllow;
      } catch(e) { console.error('Param fetch error:', e); }
    }
    
    // Apply settings
    async function applySettings() {
      const status = document.getElementById('status');
      try {
        const body = {
          numDetents: parseInt(document.getElementById('detents').value),
          detentStrength: parseFloat(document.getElementById('strength').value),
          deadZone: parseFloat(document.getElementById('deadZone').value),
          springStiffness: parseFloat(document.getElementById('stiffness').value),
          damping: parseFloat(document.getElementById('damping').value),
          overshootAllow: parseFloat(document.getElementById('overshoot').value)
        };
        const res = await fetch('/api/params', {
          method: 'POST',
          headers: {'Content-Type': 'application/json'},
          body: JSON.stringify(body)
        });
        if (res.ok) {
          status.className = 'status success';
          status.textContent = '✓ Settings applied!';
        } else {
          throw new Error('Failed');
        }
      } catch(e) {
        status.className = 'status error';
        status.textContent = '✗ Error applying settings';
      }
      setTimeout(() => status.className = 'status', 2000);
    }
    
    // Start polling
    fetchParams();
    setInterval(fetchData, 100);
  </script>
</body>
</html>
)rawliteral";

// Calculate detent spacing
float getDetentSpacing() {
  return (2.0 * PI) / numDetents;
}

// Get the nearest detent angle for a given position
float getNearestDetent(float angle) {
  float spacing = getDetentSpacing();
  // Normalize angle to 0-2PI range
  float normAngle = fmod(angle, 2.0 * PI);
  if (normAngle < 0) normAngle += 2.0 * PI;
  // Find nearest detent
  int detentIndex = round(normAngle / spacing);
  return detentIndex * spacing;
}

// Calculate springy haptic torque
float calculateDetentTorque(float angle, float velocity) {
  float spacing = getDetentSpacing();
  float nearestDetent = getNearestDetent(angle);
  
  // Normalize current angle
  float normAngle = fmod(angle, 2.0 * PI);
  if (normAngle < 0) normAngle += 2.0 * PI;
  
  // Distance from nearest detent
  float error = normAngle - nearestDetent;
  
  // Handle wraparound
  if (error > PI) error -= 2.0 * PI;
  if (error < -PI) error += 2.0 * PI;
  
  // Dead zone - no torque when very close to detent (reduces heat!)
  if (abs(error) < deadZone && abs(velocity) < 0.5) {
    return 0;
  }
  
  // Normalize error relative to detent spacing
  float normalizedError = error / (spacing * 0.5);
  
  // Non-linear spring force using sine wave for smooth elastic feel
  // Creates a smooth "potential well" at each detent
  float springForce = -sin(normalizedError * PI * overshootAllow) * detentStrength * springStiffness;
  
  // Add velocity damping to prevent oscillation and create smooth motion
  float dampingForce = -velocity * damping;
  
  // Combine forces
  float torque = springForce + dampingForce;
  
  // Soft limit with gradual rolloff
  float maxTorque = detentStrength * 1.5;
  torque = maxTorque * tanh(torque / maxTorque);
  
  return torque;
}

// ===== WEB SERVER SETUP =====
void setupWebServer() {
  // Serve the main page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", index_html);
  });
  
  // API: Get current position data
  server.on("/api/data", HTTP_GET, [](AsyncWebServerRequest *request){
    JsonDocument doc;
    doc["angle"] = currentAngle;
    doc["velocity"] = currentVelocity;
    doc["detent"] = currentDetent + 1;  // 1-indexed for display
    doc["numDetents"] = numDetents;
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });
  
  // API: Get current parameters
  server.on("/api/params", HTTP_GET, [](AsyncWebServerRequest *request){
    JsonDocument doc;
    doc["numDetents"] = numDetents;
    doc["detentStrength"] = detentStrength;
    doc["deadZone"] = deadZone;
    doc["springStiffness"] = springStiffness;
    doc["damping"] = damping;
    doc["overshootAllow"] = overshootAllow;
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });
  
  // API: Update parameters (POST with JSON body)
  server.on("/api/params", HTTP_POST, [](AsyncWebServerRequest *request){},
    NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, data, len);
      
      if (error) {
        request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        return;
      }
      
      // Update parameters if present in JSON
      if (doc.containsKey("numDetents")) {
        int val = doc["numDetents"];
        if (val >= 2 && val <= 36) numDetents = val;
      }
      if (doc.containsKey("detentStrength")) {
        float val = doc["detentStrength"];
        if (val >= 0 && val <= 5) detentStrength = val;
      }
      if (doc.containsKey("deadZone")) {
        float val = doc["deadZone"];
        if (val >= 0 && val <= 0.2) deadZone = val;
      }
      if (doc.containsKey("springStiffness")) {
        float val = doc["springStiffness"];
        if (val >= 0.1 && val <= 3) springStiffness = val;
      }
      if (doc.containsKey("damping")) {
        float val = doc["damping"];
        if (val >= 0 && val <= 0.2) damping = val;
      }
      if (doc.containsKey("overshootAllow")) {
        float val = doc["overshootAllow"];
        if (val >= 0.1 && val <= 1) overshootAllow = val;
      }
      
      Serial.println(F("Parameters updated via API"));
      request->send(200, "application/json", "{\"status\":\"ok\"}");
    }
  );
  
  // Enable CORS for external applications
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "Content-Type");
  
  server.begin();
  Serial.println(F("Web server started"));
}

void setupWiFi() {
  #if USE_STATION_MODE
    // Connect to existing WiFi network
    Serial.print(F("Connecting to WiFi: "));
    Serial.println(STA_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(STA_SSID, STA_PASSWORD);
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
      delay(500);
      Serial.print(".");
      attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println(F("\nWiFi connected!"));
      Serial.print(F("IP Address: "));
      Serial.println(WiFi.localIP());
    } else {
      Serial.println(F("\nWiFi connection failed! Starting AP mode..."));
      WiFi.mode(WIFI_AP);
      WiFi.softAP(AP_SSID, AP_PASSWORD);
      Serial.print(F("AP IP Address: "));
      Serial.println(WiFi.softAPIP());
    }
  #else
    // Access Point mode - ESP32 creates its own network
    Serial.println(F("Starting WiFi Access Point..."));
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASSWORD);
    Serial.print(F("AP Name: "));
    Serial.println(AP_SSID);
    Serial.print(F("AP Password: "));
    Serial.println(AP_PASSWORD);
    Serial.print(F("AP IP Address: "));
    Serial.println(WiFi.softAPIP());
  #endif
}

void setup() { 
  Serial.begin(115200);
  delay(2000);
  Serial.println(F("\n\nInitializing Haptic Knob with Detents..."));

  // Initialize WiFi first (non-blocking after setup)
  setupWiFi();
  
  // Initialize I2C
  Wire.begin(22, 21);  // SDA, SCL
  Wire.setClock(100000);
  
  // Initialize sensor
  sensor.init();
  Serial.println(F("AS5600 sensor initialized"));

  // Manually set enable pin HIGH
  pinMode(EN, OUTPUT);
  digitalWrite(EN, HIGH);

  driver.voltage_power_supply = 12;
  driver.voltage_limit = 12;
  
  if (!driver.init()) {
    Serial.println(F("Driver init failed!"));
    while(1);
  }
  Serial.println(F("Driver initialized"));

  driver.enable();

  // Link sensor and driver to motor
  motor.linkSensor(&sensor);
  motor.linkDriver(&driver);
  
  // Use torque control for haptic feedback
  motor.controller = MotionControlType::torque;
  motor.voltage_limit = 5;
  
  if (!motor.init()) {
    Serial.println(F("Motor init failed!"));
    while(1);
  }

  // Calibrate sensor
  motor.initFOC();
  
  // Start web server after motor is ready
  setupWebServer();
  
  Serial.println(F("\n========================================"));
  Serial.println(F("Haptic Knob Ready!"));
  Serial.print(F("Detents: "));
  Serial.println(numDetents);
  Serial.println(F("Serial: '+' more detents, '-' fewer"));
  Serial.println(F("========================================"));
  Serial.println(F("Web Interface:"));
  #if USE_STATION_MODE
    Serial.print(F("  http://"));
    Serial.println(WiFi.localIP());
  #else
    Serial.print(F("  1. Connect to WiFi: ")); Serial.println(AP_SSID);
    Serial.print(F("  2. Password: ")); Serial.println(AP_PASSWORD);
    Serial.println(F("  3. Open: http://192.168.4.1"));
  #endif
  Serial.println(F("========================================\n"));
}

unsigned long lastPrint = 0;
int lastDetent = -1;

void loop() {
  // Main FOC loop
  motor.loopFOC();
  
  // Calculate and apply springy haptic detent torque
  float angle = sensor.getAngle();
  float velocity = motor.shaft_velocity;
  float torque = calculateDetentTorque(angle, velocity);
  motor.move(torque);
  
  // Normalize angle to 0-2π (0-360°) range for display
  float normAngle = fmod(angle, 2.0 * PI);
  if (normAngle < 0) normAngle += 2.0 * PI;
  
  // Update global state for web API
  currentAngle = normAngle;
  currentVelocity = velocity;
  
  // Track current detent for feedback
  float spacing = getDetentSpacing();
  int detent = (int)round(fmod(angle, 2.0 * PI) / spacing) % numDetents;
  if (detent < 0) detent += numDetents;
  currentDetent = detent;
  
  // Print when detent changes
  if (detent != lastDetent) {
    lastDetent = detent;
    Serial.print(F("Detent: "));
    Serial.print(detent + 1);
    Serial.print(F(" / "));
    Serial.println(numDetents);
  }
  
  // Handle serial commands to change detent count
  if (Serial.available()) {
    char c = Serial.read();
    if (c == '+' && numDetents < 36) {
      numDetents++;
      Serial.print(F("Detents: "));
      Serial.println(numDetents);
    } else if (c == '-' && numDetents > 2) {
      numDetents--;
      Serial.print(F("Detents: "));
      Serial.println(numDetents);
    }
  }
  
  // Periodic status
  if (millis() - lastPrint > 1000) {
    lastPrint = millis();
    Serial.print(F("Angle: "));
    Serial.print(normAngle, 2);
    Serial.print(F(" rad | Detent: "));
    Serial.print(detent + 1);
    Serial.print(F("/"));
    Serial.println(numDetents);
  }
}