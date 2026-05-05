/*
 * Wheelchair remote + RFID / keypad (ESP32)
 *
 * SIMULATE_JOYSTICK = 1 (default): no dacWrite on GPIO25/26 — keypad can stay on
 * the original row pins {32,33,25,26}. Ramp + watchdog + web logic run; values are
 * logged on Serial (and briefly on LCD while moving). No hardware rewiring.
 *
 * SIMULATE_JOYSTICK = 0: real internal DAC on 25/26. You MUST move keypad rows off
 * 25/26 (e.g. to 17/18) — those pins cannot be shared with the keypad matrix.
 *
 * Logical voltages: STOP 2.5 V, axis ends 1.0 V / 4.0 V. Real ~4 V at connector
 * needs MCP4725 @ 5 V or an analog front-end.
 */

#ifndef SIMULATE_JOYSTICK
#define SIMULATE_JOYSTICK 1
#endif

#include <WiFi.h>
#include <math.h>
#include <string.h>
#include <WebServer.h>
#include <Keypad.h>
#include <LiquidCrystal_I2C.h>
#include <SPI.h>
#include <MFRC522.h>

// ------ Joystick / DAC ------
static const int PIN_DAC_X = 25;  // DAC1
static const int PIN_DAC_Y = 26;  // DAC2
static const float V_CENTER = 2.5f;
static const float V_MIN = 1.0f;
static const float V_MAX = 4.0f;

static const unsigned long WATCHDOG_MS = 500;
static const float RAMP_VOLTS_PER_SEC = 2.8f;

static float g_targetXV = V_CENTER;
static float g_targetYV = V_CENTER;
static float g_currentXV = V_CENTER;
static float g_currentYV = V_CENTER;
static unsigned long g_lastDriveMs = 0;
static unsigned long g_lastRampMs = 0;
static bool g_emergencyLatched = false;

// ------ WiFi AP Settings ------
const char* apSsid = "Wheelchair_CTRL";
const char* apPassword = "wheelchair123";
WebServer server(80);

// ------ Keypad (original wiring when SIMULATE_JOYSTICK: rows use 25/26) ------
const byte ROWS = 4;
const byte COLS = 4;
char keys[ROWS][COLS] = {
  {'1', '2', '3', 'A'},
  {'4', '5', '6', 'B'},
  {'7', '8', '9', 'C'},
  {'*', '0', '#', 'D'}
};
#if SIMULATE_JOYSTICK
byte rowPins[ROWS] = {32, 33, 25, 26};
#else
byte rowPins[ROWS] = {32, 33, 17, 18};
#endif
byte colPins[COLS] = {27, 14, 12, 13};
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// ------ LED Setup ------
const int ledPin = 4;
bool ledState = false;
unsigned long ledOnTime = 0;

// ------ LCD Setup ------
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ------ RFID Setup ------
#define RST_PIN 5
#define SS_PIN 15
MFRC522 rfid(SS_PIN, RST_PIN);

// ------ Password ------
const String correctPassword = "1234";
String inputPassword = "";
String hiddenPassword = "";
int failedAttempts = 0;
bool isLockedOut = false;
unsigned long lockoutStartTime = 0;

// ------ RFID UI (non-blocking) ------
unsigned long rfidUiHoldUntil = 0;
bool rfidUiActive = false;

// ------ Buzzer ------
const int buzzerPin = 16;
void beep(int duration = 100) {
  digitalWrite(buzzerPin, HIGH);
  delay(duration);
  digitalWrite(buzzerPin, LOW);
}

// ------ Allowed UIDs ------
const String allowedUIDs[] = {
  "F332D0F7",
  "EC27F09B",
  "11223344"
};

// Map logical 1.0–4.0 V command to ~0–3.3 V at the DAC pin (full swing).
static uint8_t logicalVoltsToDac8(float v) {
  v = constrain(v, V_MIN, V_MAX);
  float t = (v - V_MIN) / (V_MAX - V_MIN);
  return (uint8_t)(t * 255.0f);
}

static bool simMotionActive() {
  const float e = 0.03f;
  return fabsf(g_targetXV - V_CENTER) > e || fabsf(g_targetYV - V_CENTER) > e ||
         fabsf(g_currentXV - V_CENTER) > e || fabsf(g_currentYV - V_CENTER) > e;
}

static void applyDacXY(float logicalXV, float logicalYV) {
#if SIMULATE_JOYSTICK
  (void)logicalXV;
  (void)logicalYV;
  // No GPIO25/26 dacWrite — keypad and "virtual joystick" can coexist.
#else
  dacWrite(PIN_DAC_X, logicalVoltsToDac8(logicalXV));
  dacWrite(PIN_DAC_Y, logicalVoltsToDac8(logicalYV));
#endif
}

// HUD while web drive is active (line 0 = direction text; line 2 updated while ramping).
static bool g_driveHudActive = false;

static void simLogAndOptionalLcd() {
#if SIMULATE_JOYSTICK
  static unsigned long lastSimMs = 0;
  unsigned long now = millis();
  if (now - lastSimMs < 200) return;
  lastSimMs = now;

  if (simMotionActive()) {
    uint8_t bx = logicalVoltsToDac8(g_currentXV);
    uint8_t by = logicalVoltsToDac8(g_currentYV);
    Serial.printf("[SIM] X=%.2fV Y=%.2fV (DAC8 %u,%u) tgt %.2f %.2f\n",
                  g_currentXV, g_currentYV, bx, by, g_targetXV, g_targetYV);
  }
#endif
}

// Line 0: English only (16 chars for LCD).
static void driveHudLine0(char out[17], float nx, float ny) {
  const float t = 0.35f;
  const bool stop = fabsf(nx) < t && fabsf(ny) < t;
  if (stop) {
    snprintf(out, 17, "STOP / idle     ");
    return;
  }
  const bool left = nx < -t;
  const bool right = nx > t;
  const bool fwd = ny > t;
  const bool back = ny < -t;
  int n = (left ? 1 : 0) + (right ? 1 : 0) + (fwd ? 1 : 0) + (back ? 1 : 0);
  if (n > 1) {
    snprintf(out, 17, "DIAG pressed    ");
    return;
  }
  if (left) snprintf(out, 17, "LEFT pressed    ");
  else if (right) snprintf(out, 17, "RIGHT pressed   ");
  else if (fwd) snprintf(out, 17, "FORWARD pressed ");
  else if (back) snprintf(out, 17, "BACK pressed    ");
  else snprintf(out, 17, "STOP / idle     ");
}

// Line 1: volts + IDLE (centered) or MOVE (ramping / off-center).
static void driveHudLine1(char out[17]) {
  const char* st = simMotionActive() ? "MOVE" : "IDLE";
  snprintf(out, 17, "%.2fV %.2fV %s", g_currentXV, g_currentYV, st);
  for (int i = strlen(out); i < 16; i++) out[i] = ' ';
  out[16] = '\0';
}

static void lcdPaintDriveHud(float nx, float ny) {
  if (rfidUiActive || g_emergencyLatched) return;
  g_driveHudActive = true;
  char row0[17];
  driveHudLine0(row0, nx, ny);
  lcd.setCursor(0, 0);
  lcd.print(row0);
  char row1[17];
  driveHudLine1(row1);
  lcd.setCursor(0, 1);
  lcd.print(row1);
}

static void lcdRefreshDriveHudLine2() {
  if (!g_driveHudActive || g_emergencyLatched || rfidUiActive || g_lastDriveMs == 0) return;
  static unsigned long last = 0;
  unsigned long now = millis();
  if (now - last < 120) return;
  last = now;
  char row1[17];
  driveHudLine1(row1);
  lcd.setCursor(0, 1);
  lcd.print(row1);
}

static void serialLogDrive(float nx, float ny) {
  const float t = 0.35f;
  const bool stop = fabsf(nx) < t && fabsf(ny) < t;
  const char* dir = "STOP";
  if (!stop) {
    const bool left = nx < -t, right = nx > t, fwd = ny > t, back = ny < -t;
    int n = (left ? 1 : 0) + (right ? 1 : 0) + (fwd ? 1 : 0) + (back ? 1 : 0);
    if (n > 1) dir = "DIAGONAL";
    else if (left) dir = "LEFT";
    else if (right) dir = "RIGHT";
    else if (fwd) dir = "FORWARD";
    else if (back) dir = "BACK";
    else dir = "MIXED";
  }
  const char* motion = simMotionActive() ? "MOVE" : "IDLE";
  Serial.printf("[Drive] %s pressed | X=%.2fV Y=%.2fV | %s\n", dir, g_currentXV, g_currentYV, motion);
}

static void forceStopOutputs() {
  g_targetXV = g_currentXV = V_CENTER;
  g_targetYV = g_currentYV = V_CENTER;
  applyDacXY(V_CENTER, V_CENTER);
}

static void triggerEmergencyStop() {
  g_emergencyLatched = true;
  g_driveHudActive = false;
  forceStopOutputs();
  lcd.setCursor(0, 0);
  lcd.print("EMERGENCY STOP  ");
  lcd.setCursor(0, 1);
  lcd.print("SAFE 2.5V       ");
  beep(400);
  delay(150);
  beep(400);
}

static void clearEmergencyIfDriving() {
  if (g_emergencyLatched) g_emergencyLatched = false;
}

// Normalize -1..1 -> logical volts 1.0..4.0 (center 2.5).
static float normToVolts(float n) {
  n = constrain(n, -1.0f, 1.0f);
  return V_CENTER + n * (V_MAX - V_CENTER);  // 2.5 + n*1.5 -> [1.0,4.0]
}

static void updateRampAndDac() {
  unsigned long now = millis();
  if (g_lastRampMs == 0) g_lastRampMs = now;
  float dt = (now - g_lastRampMs) / 1000.0f;
  g_lastRampMs = now;
  if (dt > 0.05f) dt = 0.05f;

  float step = RAMP_VOLTS_PER_SEC * dt;
  float dx = g_targetXV - g_currentXV;
  float dy = g_targetYV - g_currentYV;
  if (dx > step) dx = step;
  else if (dx < -step) dx = -step;
  if (dy > step) dy = step;
  else if (dy < -step) dy = -step;
  g_currentXV += dx;
  g_currentYV += dy;
  applyDacXY(g_currentXV, g_currentYV);
  simLogAndOptionalLcd();
  lcdRefreshDriveHudLine2();
}

static void refreshDriveWatchdog() {
  g_lastDriveMs = millis();
}

static void checkDriveWatchdog() {
  if (g_lastDriveMs == 0) return;
  unsigned long now = millis();
  if (now - g_lastDriveMs > WATCHDOG_MS) {
    triggerEmergencyStop();
    g_lastDriveMs = 0;
  }
}

// ------ Web Page (wheelchair + legacy LED) ------
String webPage() {
  return R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Wheelchair + LED</title>
  <style>
    body { background: #282c34; color: white; font-family: sans-serif; text-align: center; padding: 16px; }
    button { font-size: 18px; padding: 12px 18px; margin: 8px; border: none; border-radius: 8px; touch-action: manipulation; }
    .fwd { background: #2e7d32; color: white; }
    .back { background: #c62828; color: white; }
    .lr { background: #1565c0; color: white; }
    .stop { background: #ef6c00; color: white; width: 90%; max-width: 420px; }
    .ledg { background: green; color: white; }
    .ledr { background: #b71c1c; color: white; }
    #status { margin-top: 16px; font-size: 16px; color: #ffeb3b; }
    .hint { font-size: 13px; color: #b0bec5; margin-top: 12px; }
  </style>
</head>
<body>
  <h2>Drive (hold buttons)</h2>
  <div>
    <button class="fwd" id="btnN" onpointerdown="press(0,1)" onpointerup="release()" onpointerleave="release()">Forward</button>
  </div>
  <div>
    <button class="lr" id="btnW" onpointerdown="press(-1,0)" onpointerup="release()" onpointerleave="release()">Left</button>
    <button class="stop" id="btnS" onclick="sendStop()">STOP</button>
    <button class="lr" id="btnE" onpointerdown="press(1,0)" onpointerup="release()" onpointerleave="release()">Right</button>
  </div>
  <div>
    <button class="back" id="btnB" onpointerdown="press(0,-1)" onpointerup="release()" onpointerleave="release()">Back</button>
  </div>
  <p class="hint">Sends /drive every 150 ms while held. If Wi‑Fi drops &gt;500 ms, chair stops.</p>

  <h3>LED</h3>
  <button class="ledg" onclick="controlLED('on')">Turn On</button>
  <button class="ledr" onclick="controlLED('off')">Turn Off</button>
  <div id="status">Status: —</div>

  <script>
    let tx = 0, ty = 0, timer = null;
    function drive(nx, ny) {
      tx = nx; ty = ny;
      fetch('/drive?x=' + encodeURIComponent(tx) + '&y=' + encodeURIComponent(ty))
        .then(r => r.text())
        .then(t => { document.getElementById('status').innerText = 'Drive: ' + t; })
        .catch(() => { document.getElementById('status').innerText = 'Connection error'; });
    }
    function sendStop() {
      if (timer) { clearInterval(timer); timer = null; }
      tx = 0; ty = 0;
      drive(0, 0);
    }
    function press(nx, ny) {
      if (timer) clearInterval(timer);
      drive(nx, ny);
      timer = setInterval(() => drive(nx, ny), 150);
    }
    function release() {
      if (timer) { clearInterval(timer); timer = null; }
      sendStop();
    }
    // Continuous link check: page must refresh /drive at least every 500 ms (spec).
    setInterval(() => { if (!timer) drive(0, 0); }, 200);
    function controlLED(action) {
      fetch('/' + action)
        .then(response => response.text())
        .then(data => { document.getElementById('status').innerText = 'LED: ' + data; })
        .catch(() => { document.getElementById('status').innerText = 'Connection Error!'; });
    }
  </script>
</body>
</html>
)rawliteral";
}

void handleRoot() {
  server.send(200, "text/html; charset=utf-8", webPage());
}

static void handleDrive() {
  if (!server.hasArg("x") || !server.hasArg("y")) {
    server.send(400, "text/plain; charset=utf-8", "missing x,y");
    return;
  }
  float nx = server.arg("x").toFloat();
  float ny = server.arg("y").toFloat();
  clearEmergencyIfDriving();
  refreshDriveWatchdog();
  g_targetXV = normToVolts(nx);
  g_targetYV = normToVolts(ny);

  lcdPaintDriveHud(nx, ny);
  serialLogDrive(nx, ny);

  server.send(200, "text/plain; charset=utf-8", "ok");
}

void handleOn() {
  digitalWrite(ledPin, HIGH);
  ledState = true;
  ledOnTime = millis();
  lcd.setCursor(0, 1);
  lcd.print("LED is ON       ");
  beep(200);
  server.send(200, "text/plain; charset=utf-8", "LED is ON");
}

void handleOff() {
  digitalWrite(ledPin, LOW);
  ledState = false;
  lcd.setCursor(0, 1);
  lcd.print("LED is OFF      ");
  beep(200);
  server.send(200, "text/plain; charset=utf-8", "LED is OFF");
}

void setupWiFi() {
  WiFi.mode(WIFI_AP);
  bool apOk = WiFi.softAP(apSsid, apPassword);
  IPAddress apIp = WiFi.softAPIP();

  if (!apOk) {
    Serial.println("Failed to start AP");
  } else {
    Serial.print("AP started. SSID: ");
    Serial.println(apSsid);
    Serial.print("AP IP: ");
    Serial.println(apIp);
  }

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("AP Ready");
  lcd.setCursor(0, 1);
  lcd.print(apIp);
  delay(5000);
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Enter Password:");
}

bool isUIDAllowed(String uid) {
  for (String allowed : allowedUIDs) {
    if (uid == allowed) return true;
  }
  return false;
}

void setup() {
  Serial.begin(115200);
  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, LOW);
  pinMode(buzzerPin, OUTPUT);
  digitalWrite(buzzerPin, LOW);

  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Booting...");

  forceStopOutputs();
  g_lastRampMs = millis();
  g_lastDriveMs = 0;

  SPI.begin();
  rfid.PCD_Init();

  setupWiFi();
  server.on("/", handleRoot);
  server.on("/drive", handleDrive);
  server.on("/on", handleOn);
  server.on("/off", handleOff);
  server.begin();
  Serial.println("HTTP Server started");
#if SIMULATE_JOYSTICK
  Serial.println("Joystick: SIMULATION (no DAC on 25/26). Set SIMULATE_JOYSTICK 0 for real DAC + rewire keypad rows.");
#else
  Serial.println("Joystick: REAL DAC on GPIO25/26.");
#endif
}

void loop() {
  server.handleClient();
  updateRampAndDac();
  checkDriveWatchdog();

  unsigned long currentTime = millis();

  // RFID Section (non-blocking hold after read)
  if (rfidUiActive) {
    if (currentTime >= rfidUiHoldUntil) {
      rfidUiActive = false;
      lcd.setCursor(0, 0);
      lcd.print("Enter Password: ");
      lcd.setCursor(0, 1);
      lcd.print("                ");
    }
  } else if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
    String uid = "";
    for (byte i = 0; i < rfid.uid.size; i++) {
      if (rfid.uid.uidByte[i] < 0x10) uid += "0";
      uid += String(rfid.uid.uidByte[i], HEX);
    }
    uid.toUpperCase();
    Serial.println("UID: " + uid);
    lcd.setCursor(0, 1);
    lcd.print("UID: " + uid);
    if (isUIDAllowed(uid)) {
      digitalWrite(ledPin, HIGH);
      ledState = true;
      ledOnTime = currentTime;
      lcd.setCursor(0, 0);
      lcd.print("Access Granted  ");
      beep(300);
    } else {
      lcd.setCursor(0, 0);
      lcd.print("Access Denied   ");
      beep(100);
      delay(100);
      beep(100);
    }
    rfidUiHoldUntil = currentTime + 2000;
    rfidUiActive = true;
    rfid.PICC_HaltA();
  }

  if (isLockedOut) {
    if (currentTime - lockoutStartTime >= 10UL * 60UL * 1000UL) {
      isLockedOut = false;
      failedAttempts = 0;
      lcd.setCursor(0, 0);
      lcd.print("Try again:      ");
    }
    return;
  }

  char key = keypad.getKey();
  if (key) {
    beep(80);
    if (key == 'D') {
      inputPassword = "";
      hiddenPassword = "";
      lcd.setCursor(0, 0);
      lcd.print("Cleared         ");
    } else if (key == 'A') {
      if (inputPassword == correctPassword) {
        digitalWrite(ledPin, HIGH);
        ledState = true;
        ledOnTime = currentTime;
        lcd.setCursor(0, 0);
        lcd.print("Correct! LED ON ");
        beep(250);
        failedAttempts = 0;
      } else {
        lcd.setCursor(0, 0);
        lcd.print("Wrong! Try again");
        beep(100);
        delay(100);
        beep(100);
        failedAttempts++;
        if (failedAttempts >= 4) {
          isLockedOut = true;
          lockoutStartTime = currentTime;
          lcd.setCursor(0, 0);
          lcd.print("LOCKED 10 MIN   ");
        }
      }
      inputPassword = "";
      hiddenPassword = "";
    } else if (key >= '0' && key <= '9') {
      if (inputPassword.length() < 10) {
        inputPassword += key;
        hiddenPassword += "*";
        lcd.setCursor(0, 0);
        lcd.print("Input: " + hiddenPassword);
      }
    }
  }

  if (ledState && currentTime - ledOnTime >= 30000) {
    digitalWrite(ledPin, LOW);
    ledState = false;
    lcd.setCursor(0, 1);
    lcd.print("LED is OFF      ");
  }
}
