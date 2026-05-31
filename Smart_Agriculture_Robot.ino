#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <ArduinoJson.h>

// ---------------- WiFi AP ----------------
const char* ssid = "Smart Agriculture Robot";
const char* password = "12345678";

// ---------------- Motor Pins ----------------
#define IN1 18
#define IN2 19
#define IN3 21
#define IN4 22

// ---------------- Relay Pins ----------------
#define RELAY1_PIN 25
#define RELAY2_PIN 26

// ---------------- IR Sensor Pins ----------------
#define LEFT_IR_PIN 32
#define RIGHT_IR_PIN 33

// ---------------- Soil Sensor Pin ----------------
#define SOIL_SENSOR_PIN 34

// ---------------- Rain Sensor Pin ----------------
#define RAIN_SENSOR_PIN 35

// ---------------- Ultrasonic Sensor Pins ----------------
#define ULTRASONIC_TRIG_PIN 5
#define ULTRASONIC_ECHO_PIN 23

// ---------------- Settings ----------------
#define OBSTACLE_STOP_DISTANCE_CM 15

#define SOIL_DRY_VALUE 3500
#define SOIL_WET_VALUE 1300

#define RAIN_DRY_VALUE 4095
#define RAIN_WET_VALUE 1500

// Most IR sensors output LOW when detected.
// If your IR sensor gives HIGH when detected, change LOW to HIGH.
#define IR_DETECTED_STATE LOW

// Most relay modules are Active LOW.
// If relay works opposite, swap LOW and HIGH.
#define RELAY_ON LOW
#define RELAY_OFF HIGH

WebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81);

// ---------------- Variables ----------------
String motorStatus = "Stopped";

bool relay1State = false;
bool relay2State = false;
bool autoMode = false;

bool leftIRDetected = false;
bool rightIRDetected = false;

int soilRawValue = 0;
int soilMoisturePercent = 0;
String soilStatus = "Unknown";

int rainRawValue = 0;
int rainPercent = 0;
String rainStatus = "Unknown";

float distanceCm = -1;
String ultrasonicStatus = "Unknown";

unsigned long lastSensorReadTime = 0;
unsigned long lastDashboardSendTime = 0;

// ---------------- Function Prototypes ----------------
void sendStatus(uint8_t clientNum);
String HTMLPage();

// ---------------- Motor Functions ----------------
void stopCar() {
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, LOW);

  motorStatus = "Stopped";
}

// Forward and backward are fixed here.
// Your previous forward command physically moved backward,
// so the motor logic has been swapped.
void moveForward() {
  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, HIGH);

  motorStatus = "Moving Forward";
}

void moveBackward() {
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, HIGH);
  digitalWrite(IN3, HIGH);
  digitalWrite(IN4, LOW);

  motorStatus = "Moving Backward";
}

void turnLeft() {
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, HIGH);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, HIGH);

  motorStatus = "Turning Left";
}

void turnRight() {
  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, HIGH);
  digitalWrite(IN4, LOW);

  motorStatus = "Turning Right";
}

// ---------------- Relay Functions ----------------
void setRelay1(bool state) {
  relay1State = state;
  digitalWrite(RELAY1_PIN, relay1State ? RELAY_ON : RELAY_OFF);
}

void setRelay2(bool state) {
  relay2State = state;
  digitalWrite(RELAY2_PIN, relay2State ? RELAY_ON : RELAY_OFF);
}

// ---------------- Sensor Functions ----------------
void readIRSensors() {
  leftIRDetected = digitalRead(LEFT_IR_PIN) == IR_DETECTED_STATE;
  rightIRDetected = digitalRead(RIGHT_IR_PIN) == IR_DETECTED_STATE;
}

void readSoilSensor() {
  soilRawValue = analogRead(SOIL_SENSOR_PIN);

  soilMoisturePercent = map(
    soilRawValue,
    SOIL_DRY_VALUE,
    SOIL_WET_VALUE,
    0,
    100
  );

  soilMoisturePercent = constrain(soilMoisturePercent, 0, 100);

  if (soilMoisturePercent < 30) {
    soilStatus = "Dry";
  } 
  else if (soilMoisturePercent < 70) {
    soilStatus = "Normal";
  } 
  else {
    soilStatus = "Wet";
  }
}

void readRainSensor() {
  rainRawValue = analogRead(RAIN_SENSOR_PIN);

  rainPercent = map(
    rainRawValue,
    RAIN_DRY_VALUE,
    RAIN_WET_VALUE,
    0,
    100
  );

  rainPercent = constrain(rainPercent, 0, 100);

  if (rainPercent < 20) {
    rainStatus = "No Rain";
  } 
  else if (rainPercent < 60) {
    rainStatus = "Light Rain";
  } 
  else {
    rainStatus = "Heavy Rain";
  }
}

void readUltrasonicSensor() {
  digitalWrite(ULTRASONIC_TRIG_PIN, LOW);
  delayMicroseconds(2);

  digitalWrite(ULTRASONIC_TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(ULTRASONIC_TRIG_PIN, LOW);

  long duration = pulseIn(ULTRASONIC_ECHO_PIN, HIGH, 20000);

  if (duration == 0) {
    distanceCm = -1;
    ultrasonicStatus = "Out of Range";
  } 
  else {
    distanceCm = duration * 0.0343 / 2.0;

    if (distanceCm <= OBSTACLE_STOP_DISTANCE_CM) {
      ultrasonicStatus = "Obstacle Close";
    } 
    else {
      ultrasonicStatus = "Clear";
    }
  }
}

void readAllSensors() {
  readIRSensors();
  readSoilSensor();
  readRainSensor();
  readUltrasonicSensor();
}

// ---------------- Auto Mode Logic ----------------
void autoModeControl() {
  if (!autoMode) {
    return;
  }

  String oldMotorStatus = motorStatus;

  if (distanceCm > 0 && distanceCm <= OBSTACLE_STOP_DISTANCE_CM) {
    stopCar();
    motorStatus = "Obstacle Stopped";

    if (oldMotorStatus != motorStatus) {
      sendStatus(255);
    }

    return;
  }

  if (!leftIRDetected && !rightIRDetected) {
    stopCar();
  }

  else if (leftIRDetected && rightIRDetected) {
    moveForward();
  }

  else if (!leftIRDetected && rightIRDetected) {
    turnRight();
  }

  else if (leftIRDetected && !rightIRDetected) {
    turnLeft();
  }

  if (oldMotorStatus != motorStatus) {
    sendStatus(255);
  }
}

// ---------------- Send Status to Dashboard ----------------
void sendStatus(uint8_t clientNum) {
  StaticJsonDocument<1536> doc;

  doc["motor"] = motorStatus;
  doc["autoMode"] = autoMode;

  doc["relay1"] = relay1State;
  doc["relay2"] = relay2State;

  doc["leftIR"] = leftIRDetected;
  doc["rightIR"] = rightIRDetected;

  doc["soilRaw"] = soilRawValue;
  doc["soilPercent"] = soilMoisturePercent;
  doc["soilStatus"] = soilStatus;

  doc["rainRaw"] = rainRawValue;
  doc["rainPercent"] = rainPercent;
  doc["rainStatus"] = rainStatus;

  doc["distanceCm"] = distanceCm;
  doc["ultrasonicStatus"] = ultrasonicStatus;

  String jsonString;
  serializeJson(doc, jsonString);

  if (clientNum == 255) {
    webSocket.broadcastTXT(jsonString);
  } else {
    webSocket.sendTXT(clientNum, jsonString);
  }
}

// ---------------- WebSocket Event ----------------
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  if (type == WStype_DISCONNECTED) {
    Serial.printf("[%u] Disconnected\n", num);

    if (!autoMode) {
      stopCar();
    }
  }

  else if (type == WStype_CONNECTED) {
    IPAddress ip = webSocket.remoteIP(num);

    Serial.printf("[%u] Connected from %d.%d.%d.%d\n",
                  num, ip[0], ip[1], ip[2], ip[3]);

    sendStatus(num);
  }

  else if (type == WStype_TEXT) {
    StaticJsonDocument<256> doc;
    DeserializationError error = deserializeJson(doc, payload, length);

    if (error) {
      Serial.print("JSON Error: ");
      Serial.println(error.f_str());
      return;
    }

    if (doc.containsKey("autoMode")) {
      autoMode = doc["autoMode"];
      stopCar();

      if (autoMode) {
        motorStatus = "Auto Mode Started";
      } else {
        motorStatus = "Manual Mode Started";
      }
    }

    if (doc.containsKey("motor") && !autoMode) {
      const char* motorCommand = doc["motor"];

      if (strcmp(motorCommand, "forward") == 0) {
        moveForward();
      }

      else if (strcmp(motorCommand, "backward") == 0) {
        moveBackward();
      }

      else if (strcmp(motorCommand, "left") == 0) {
        turnLeft();
      }

      else if (strcmp(motorCommand, "right") == 0) {
        turnRight();
      }

      else if (strcmp(motorCommand, "stop") == 0) {
        stopCar();
      }
    }

    if (doc.containsKey("relay1")) {
      bool state = doc["relay1"];
      setRelay1(state);
    }

    if (doc.containsKey("relay2")) {
      bool state = doc["relay2"];
      setRelay2(state);
    }

    sendStatus(255);
  }
}

// ---------------- Web Dashboard Page ----------------
String HTMLPage() {
  String page = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">

<title>Smart Agriculture Robot</title>

<style>
:root {
  --bg: #f4f7f1;
  --card: #ffffff;
  --green: #1f7a4d;
  --green-dark: #145c38;
  --blue: #2563eb;
  --orange: #f97316;
  --red: #dc2626;
  --gray: #64748b;
  --text: #102016;
  --muted: #5f6f64;
  --border: #d8e6dc;
}

* {
  box-sizing: border-box;
}

body {
  font-family: Arial, sans-serif;
  background: var(--bg);
  color: var(--text);
  margin: 0;
  padding: 0;
}

.header {
  background: linear-gradient(135deg, #145c38, #1f7a4d);
  color: white;
  padding: 22px 16px;
  text-align: center;
}

.header h1 {
  margin: 0;
  font-size: 26px;
}

.header p {
  margin: 8px 0 0;
  color: #e7f5ec;
}

.container {
  width: 94%;
  max-width: 1100px;
  margin: 18px auto 30px;
}

#connectionStatus {
  padding: 12px;
  border-radius: 12px;
  margin-bottom: 16px;
  text-align: center;
  font-weight: bold;
}

.connected {
  background: #dcfce7;
  color: #166534;
  border: 1px solid #86efac;
}

.disconnected {
  background: #fee2e2;
  color: #991b1b;
  border: 1px solid #fecaca;
}

.grid {
  display: grid;
  grid-template-columns: repeat(2, 1fr);
  gap: 16px;
}

.card {
  background: var(--card);
  border: 1px solid var(--border);
  border-radius: 18px;
  padding: 18px;
  box-shadow: 0 4px 14px rgba(0,0,0,0.06);
}

.card h2 {
  margin: 0 0 12px;
  font-size: 20px;
  color: var(--green-dark);
}

.status {
  color: var(--green);
  font-weight: bold;
}

.row {
  display: flex;
  justify-content: space-between;
  gap: 12px;
  border-bottom: 1px solid #eef4ef;
  padding: 8px 0;
}

.row:last-child {
  border-bottom: none;
}

button {
  border: none;
  border-radius: 12px;
  padding: 12px 18px;
  font-size: 17px;
  cursor: pointer;
  margin: 5px;
  color: white;
  font-weight: bold;
  user-select: none;
}

button:active {
  transform: scale(0.96);
}

.modeBtn {
  width: 100%;
  background: var(--blue);
}

.autoMode {
  background: var(--orange);
}

.manualMode {
  background: var(--blue);
}

.control-grid {
  display: grid;
  grid-template-columns: repeat(3, 1fr);
  gap: 8px;
  align-items: center;
}

.move {
  width: 100%;
  min-height: 58px;
  background: var(--green);
}

.stopBtn {
  background: var(--red);
}

.empty {
  visibility: hidden;
}

.relay-wrap {
  display: grid;
  grid-template-columns: repeat(2, 1fr);
  gap: 12px;
}

.relayBtn {
  width: 100%;
  min-height: 54px;
}

.relayOn {
  background: var(--green);
}

.relayOff {
  background: var(--gray);
}

.disabledBtn {
  background: var(--gray) !important;
  cursor: not-allowed;
  opacity: 0.65;
}

.progressOuter {
  width: 100%;
  height: 22px;
  background: #e2e8f0;
  border-radius: 20px;
  overflow: hidden;
  margin-top: 8px;
}

.progressInner {
  height: 100%;
  width: 0%;
  background: var(--green);
  text-align: center;
  line-height: 22px;
  font-size: 13px;
  font-weight: bold;
  color: white;
}

.footer {
  text-align: center;
  color: var(--muted);
  margin-top: 18px;
}

@media (max-width: 800px) {
  .grid {
    grid-template-columns: 1fr;
  }

  .header h1 {
    font-size: 22px;
  }

  .card {
    padding: 16px;
  }

  button {
    font-size: 16px;
    padding: 11px 14px;
  }

  .relay-wrap {
    grid-template-columns: 1fr;
  }
}

@media (max-width: 420px) {
  .container {
    width: 96%;
  }

  .control-grid {
    gap: 6px;
  }

  .move {
    min-height: 54px;
    font-size: 14px;
  }
}
</style>
</head>

<body>

<div class="header">
  <h1>Smart Agriculture Robot</h1>
  <p>Manual and automatic control dashboard</p>
</div>

<div class="container">

<div id="connectionStatus" class="disconnected">
  Disconnected from robot
</div>

<div class="grid">

  <div class="card">
    <h2>Mode Control</h2>
    <div class="row">
      <span>Current Mode</span>
      <span class="status" id="modeStatus">MANUAL MODE</span>
    </div>

    <button id="modeButton" class="modeBtn manualMode" onclick="toggleMode()">
      Switch to Auto Mode
    </button>

    <p id="modeHint">Manual mode: use dashboard buttons.</p>
  </div>

  <div class="card">
    <h2>Motor Status</h2>
    <div class="row">
      <span>Motor</span>
      <span class="status" id="motorStatus">Stopped</span>
    </div>
  </div>

  <div class="card">
    <h2>Manual Motor Control</h2>
    <p id="manualControlText">Press and hold a button for movement.</p>

    <div class="control-grid">
      <button class="move empty">-</button>

      <button class="move manualBtn"
        onmousedown="startMotor('forward')"
        onmouseup="stopMotor()"
        onmouseleave="stopMotor()"
        ontouchstart="startMotor('forward')"
        ontouchend="stopMotor()">Forward</button>

      <button class="move empty">-</button>

      <button class="move manualBtn"
        onmousedown="startMotor('left')"
        onmouseup="stopMotor()"
        onmouseleave="stopMotor()"
        ontouchstart="startMotor('left')"
        ontouchend="stopMotor()">Left</button>

      <button class="move stopBtn manualBtn" onclick="stopMotor()">Stop</button>

      <button class="move manualBtn"
        onmousedown="startMotor('right')"
        onmouseup="stopMotor()"
        onmouseleave="stopMotor()"
        ontouchstart="startMotor('right')"
        ontouchend="stopMotor()">Right</button>

      <button class="move empty">-</button>

      <button class="move manualBtn"
        onmousedown="startMotor('backward')"
        onmouseup="stopMotor()"
        onmouseleave="stopMotor()"
        ontouchstart="startMotor('backward')"
        ontouchend="stopMotor()">Backward</button>

      <button class="move empty">-</button>
    </div>
  </div>

  <div class="card">
    <h2>Relay Control</h2>

    <div class="relay-wrap">
      <div>
        <div class="row">
          <span>Crops Cutting</span>
          <span class="status" id="relay1Status">OFF</span>
        </div>
        <button id="relay1Button" class="relayBtn relayOff" onclick="toggleRelay1()">
          Crops Cutting OFF
        </button>
      </div>

      <div>
        <div class="row">
          <span>Spray Water</span>
          <span class="status" id="relay2Status">OFF</span>
        </div>
        <button id="relay2Button" class="relayBtn relayOff" onclick="toggleRelay2()">
          Spray Water OFF
        </button>
      </div>
    </div>
  </div>

  <div class="card">
    <h2>IR Sensors</h2>
    <div class="row">
      <span>Left IR</span>
      <span class="status" id="leftIRStatus">NOT DETECTED</span>
    </div>
    <div class="row">
      <span>Right IR</span>
      <span class="status" id="rightIRStatus">NOT DETECTED</span>
    </div>
  </div>

  <div class="card">
    <h2>Ultrasonic Sensor</h2>
    <div class="row">
      <span>Distance</span>
      <span class="status" id="distanceCm">0 cm</span>
    </div>
    <div class="row">
      <span>Status</span>
      <span class="status" id="ultrasonicStatus">Unknown</span>
    </div>
    <div class="row">
      <span>Auto Stop Distance</span>
      <span class="status">15 cm</span>
    </div>
  </div>

  <div class="card">
    <h2>Soil Moisture Sensor</h2>
    <div class="row">
      <span>Moisture</span>
      <span class="status" id="soilPercent">0%</span>
    </div>
    <div class="row">
      <span>Status</span>
      <span class="status" id="soilStatus">Unknown</span>
    </div>
    <div class="row">
      <span>Raw Value</span>
      <span class="status" id="soilRaw">0</span>
    </div>

    <div class="progressOuter">
      <div class="progressInner" id="soilProgress">0%</div>
    </div>
  </div>

  <div class="card">
    <h2>Rain Sensor</h2>
    <div class="row">
      <span>Rain Level</span>
      <span class="status" id="rainPercent">0%</span>
    </div>
    <div class="row">
      <span>Status</span>
      <span class="status" id="rainStatus">Unknown</span>
    </div>
    <div class="row">
      <span>Raw Value</span>
      <span class="status" id="rainRaw">0</span>
    </div>

    <div class="progressOuter">
      <div class="progressInner" id="rainProgress">0%</div>
    </div>
  </div>

</div>

<div class="footer">
  <p>WiFi AP: <b>Smart Agriculture Robot</b></p>
  <p>Password: <b>12345678</b></p>
  <p>Open browser: <b>192.168.4.1</b></p>
</div>

</div>

<script>
var socket;
var reconnectInterval = 2000;

var relay1State = false;
var relay2State = false;
var autoMode = false;

function connectWebSocket() {
  socket = new WebSocket('ws://' + window.location.hostname + ':81/');

  socket.onopen = function() {
    document.getElementById('connectionStatus').className = 'connected';
    document.getElementById('connectionStatus').textContent = 'Connected to robot';
  };

  socket.onclose = function() {
    document.getElementById('connectionStatus').className = 'disconnected';
    document.getElementById('connectionStatus').textContent = 'Disconnected from robot. Reconnecting...';
    setTimeout(connectWebSocket, reconnectInterval);
  };

  socket.onerror = function() {
    document.getElementById('connectionStatus').className = 'disconnected';
    document.getElementById('connectionStatus').textContent = 'Connection error';
  };

  socket.onmessage = function(event) {
    var data = JSON.parse(event.data);

    if (typeof data.motor !== 'undefined') {
      document.getElementById('motorStatus').textContent = data.motor;
    }

    if (typeof data.autoMode !== 'undefined') {
      autoMode = data.autoMode;
      updateModeUI();
    }

    if (typeof data.relay1 !== 'undefined') {
      relay1State = data.relay1;
      updateRelay1UI();
    }

    if (typeof data.relay2 !== 'undefined') {
      relay2State = data.relay2;
      updateRelay2UI();
    }

    if (typeof data.leftIR !== 'undefined') {
      document.getElementById('leftIRStatus').textContent =
        data.leftIR ? 'DETECTED' : 'NOT DETECTED';
    }

    if (typeof data.rightIR !== 'undefined') {
      document.getElementById('rightIRStatus').textContent =
        data.rightIR ? 'DETECTED' : 'NOT DETECTED';
    }

    if (typeof data.soilPercent !== 'undefined') {
      updateSoilUI(data.soilPercent, data.soilRaw, data.soilStatus);
    }

    if (typeof data.rainPercent !== 'undefined') {
      updateRainUI(data.rainPercent, data.rainRaw, data.rainStatus);
    }

    if (typeof data.distanceCm !== 'undefined') {
      updateUltrasonicUI(data.distanceCm, data.ultrasonicStatus);
    }
  };
}

function sendCommand(command) {
  if (socket && socket.readyState === WebSocket.OPEN) {
    socket.send(JSON.stringify(command));
  }
}

function startMotor(direction) {
  if (autoMode) return;
  sendCommand({motor: direction});
}

function stopMotor() {
  if (autoMode) return;
  sendCommand({motor: 'stop'});
}

function toggleMode() {
  autoMode = !autoMode;
  sendCommand({autoMode: autoMode});
  updateModeUI();
}

function toggleRelay1() {
  relay1State = !relay1State;
  sendCommand({relay1: relay1State});
  updateRelay1UI();
}

function toggleRelay2() {
  relay2State = !relay2State;
  sendCommand({relay2: relay2State});
  updateRelay2UI();
}

function updateModeUI() {
  var modeStatus = document.getElementById('modeStatus');
  var modeButton = document.getElementById('modeButton');
  var modeHint = document.getElementById('modeHint');
  var manualControlText = document.getElementById('manualControlText');
  var manualButtons = document.getElementsByClassName('manualBtn');

  if (autoMode) {
    modeStatus.textContent = 'AUTO MODE';
    modeButton.textContent = 'Switch to Manual Mode';
    modeButton.className = 'modeBtn autoMode';
    modeHint.textContent = 'Auto mode: robot follows IR sensors and stops for obstacles.';
    manualControlText.textContent = 'Manual buttons are disabled in Auto Mode.';

    for (var i = 0; i < manualButtons.length; i++) {
      manualButtons[i].classList.add('disabledBtn');
    }
  } else {
    modeStatus.textContent = 'MANUAL MODE';
    modeButton.textContent = 'Switch to Auto Mode';
    modeButton.className = 'modeBtn manualMode';
    modeHint.textContent = 'Manual mode: use dashboard buttons.';
    manualControlText.textContent = 'Press and hold a button for movement.';

    for (var i = 0; i < manualButtons.length; i++) {
      manualButtons[i].classList.remove('disabledBtn');
    }
  }
}

function updateRelay1UI() {
  var status = document.getElementById('relay1Status');
  var button = document.getElementById('relay1Button');

  if (relay1State) {
    status.textContent = 'ON';
    button.textContent = 'Crops Cutting ON';
    button.className = 'relayBtn relayOn';
  } else {
    status.textContent = 'OFF';
    button.textContent = 'Crops Cutting OFF';
    button.className = 'relayBtn relayOff';
  }
}

function updateRelay2UI() {
  var status = document.getElementById('relay2Status');
  var button = document.getElementById('relay2Button');

  if (relay2State) {
    status.textContent = 'ON';
    button.textContent = 'Spray Water ON';
    button.className = 'relayBtn relayOn';
  } else {
    status.textContent = 'OFF';
    button.textContent = 'Spray Water OFF';
    button.className = 'relayBtn relayOff';
  }
}

function updateSoilUI(percent, raw, statusText) {
  document.getElementById('soilPercent').textContent = percent + '%';
  document.getElementById('soilRaw').textContent = raw;
  document.getElementById('soilStatus').textContent = statusText;

  var progress = document.getElementById('soilProgress');
  progress.style.width = percent + '%';
  progress.textContent = percent + '%';
}

function updateRainUI(percent, raw, statusText) {
  document.getElementById('rainPercent').textContent = percent + '%';
  document.getElementById('rainRaw').textContent = raw;
  document.getElementById('rainStatus').textContent = statusText;

  var progress = document.getElementById('rainProgress');
  progress.style.width = percent + '%';
  progress.textContent = percent + '%';
}

function updateUltrasonicUI(distance, statusText) {
  var distanceElement = document.getElementById('distanceCm');

  if (distance < 0) {
    distanceElement.textContent = 'Out of Range';
  } else {
    distanceElement.textContent = Number(distance).toFixed(1) + ' cm';
  }

  document.getElementById('ultrasonicStatus').textContent = statusText;
}

document.addEventListener('keydown', function(event) {
  if (autoMode) return;

  switch(event.key) {
    case 'ArrowUp':
    case 'w':
    case 'W':
      startMotor('forward');
      break;

    case 'ArrowDown':
    case 's':
    case 'S':
      startMotor('backward');
      break;

    case 'ArrowLeft':
    case 'a':
    case 'A':
      startMotor('left');
      break;

    case 'ArrowRight':
    case 'd':
    case 'D':
      startMotor('right');
      break;
  }
});

document.addEventListener('keyup', function(event) {
  if (autoMode) return;

  switch(event.key) {
    case 'ArrowUp':
    case 'ArrowDown':
    case 'ArrowLeft':
    case 'ArrowRight':
    case 'w':
    case 'W':
    case 's':
    case 'S':
    case 'a':
    case 'A':
    case 'd':
    case 'D':
      stopMotor();
      break;
  }
});

document.addEventListener('contextmenu', function(event) {
  if (event.target.classList.contains('move')) {
    event.preventDefault();
    return false;
  }
});

window.onload = function() {
  connectWebSocket();
  updateModeUI();
  updateRelay1UI();
  updateRelay2UI();
};
</script>

</body>
</html>
)rawliteral";

  return page;
}

// ---------------- Web Server Handler ----------------
void handleRoot() {
  server.send(200, "text/html", HTMLPage());
}

// ---------------- Setup ----------------
void setup() {
  Serial.begin(115200);

  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);

  pinMode(RELAY1_PIN, OUTPUT);
  pinMode(RELAY2_PIN, OUTPUT);

  pinMode(LEFT_IR_PIN, INPUT);
  pinMode(RIGHT_IR_PIN, INPUT);

  pinMode(SOIL_SENSOR_PIN, INPUT);
  pinMode(RAIN_SENSOR_PIN, INPUT);

  pinMode(ULTRASONIC_TRIG_PIN, OUTPUT);
  pinMode(ULTRASONIC_ECHO_PIN, INPUT);

  digitalWrite(ULTRASONIC_TRIG_PIN, LOW);

  analogReadResolution(12);
  analogSetPinAttenuation(SOIL_SENSOR_PIN, ADC_11db);
  analogSetPinAttenuation(RAIN_SENSOR_PIN, ADC_11db);

  stopCar();

  setRelay1(false);
  setRelay2(false);

  readAllSensors();

  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid, password);

  server.on("/", handleRoot);
  server.begin();

  webSocket.begin();
  webSocket.onEvent(webSocketEvent);

  Serial.println("HTTP server started");
  Serial.println("WebSocket server started on port 81");
  Serial.print("WiFi Name: ");
  Serial.println(ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.softAPIP());
}

// ---------------- Loop ----------------
void loop() {
  server.handleClient();
  webSocket.loop();

  if (millis() - lastSensorReadTime >= 300) {
    lastSensorReadTime = millis();
    readAllSensors();
  }

  autoModeControl();

  if (millis() - lastDashboardSendTime >= 1000) {
    lastDashboardSendTime = millis();
    sendStatus(255);
  }

  delay(5);
}