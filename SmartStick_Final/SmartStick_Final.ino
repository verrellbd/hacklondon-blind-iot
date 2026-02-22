// ============================================================
//  SmartStick_Final.ino  —  v1.0
//  MERGED: Navigation + Ultrasonic Obstacle + SOS Button
//
//  ── Features ─────────────────────────────────────────────
//  1. Navigation commands from phone (R/L/S/U/A/X)
//  2. Ultrasonic obstacle detection → buzzer + phone vibrate
//  3. SOS button → BLE notify → phone opens WhatsApp
//
//  ── BLE Commands received FROM phone ─────────────────────
//    "R"  → Turn Right     (1 high beep)
//    "L"  → Turn Left      (1 low beep)
//    "S"  → Go Straight    (double beep)
//    "U"  → U-Turn         (alternating beep)
//    "A"  → Arrived        (rising melody)
//    "X"  → Stop           (long low tone)
//
//  ── BLE Notifications sent TO phone ──────────────────────
//    "OBS:CLEAR"   → no obstacle
//    "OBS:NOTICE"  → obstacle < 150cm
//    "OBS:WARN"    → obstacle < 80cm
//    "OBS:DANGER"  → obstacle < 30cm
//    "SOS"         → SOS button pressed
//    "OK"          → nav command ACK
//
//  ── Priority ─────────────────────────────────────────────
//    SOS > Obstacle > Nav
//    DANGER/WARN obstacle pauses nav beep
//
//  ── Wiring ───────────────────────────────────────────────
//    HC-SR04 TRIG  → GPIO4
//    HC-SR04 ECHO  → GPIO5
//    Buzzer  (+)   → GPIO21
//    Buzzer  (-)   → GND
//    SOS Button    → GPIO2 + GND  (uses internal pullup)
//
//  Board: ESP32 Dev Module
// ============================================================

#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLE2902.h>

// ── Pins ─────────────────────────────────────────────────────
#define TRIG_PIN    4
#define ECHO_PIN    5
#define BUZZER_PIN  21
#define BTN_PIN     2

// ── Obstacle thresholds (cm) ──────────────────────────────────
#define DIST_DANGER   30
#define DIST_WARNING  80
#define DIST_NOTICE  150

// ── Timing ────────────────────────────────────────────────────
#define SENSOR_INTERVAL  300    // ms between ultrasonic reads
#define BTN_COOLDOWN    10000   // ms cooldown after SOS trigger

// ── BLE UUIDs ─────────────────────────────────────────────────
#define BLE_SERVICE_UUID     "12345678-1234-1234-1234-123456789abc"
#define BLE_CMD_CHAR_UUID    "12345678-1234-1234-1234-123456789ab1"
#define BLE_NOTIFY_CHAR_UUID "12345678-1234-1234-1234-123456789ab2"
#define DEVICE_NAME          "SmartStick"

// ── BLE globals ───────────────────────────────────────────────
BLEServer*         pServer     = nullptr;
BLECharacteristic* pCmdChar    = nullptr;
BLECharacteristic* pNotifyChar = nullptr;
bool bleConnected = false;
bool wasConnected = false;
String pendingCmd = "";

// ── Obstacle state ─────────────────────────────────────────────
unsigned long lastSensorTime = 0;
int  lastObsLevel = -1;

// ── SOS button state ───────────────────────────────────────────
bool btnWasDown      = false;
bool sosInCooldown   = false;
unsigned long sosLastTrigger = 0;

// ============================================================
//  BUZZER  —  passive buzzer via digitalWrite + delayMicroseconds
// ============================================================
void tone_(int freq, int ms) {
  int half = (1000000 / freq) / 2;
  long cycles = (long)freq * ms / 1000;
  for (long i = 0; i < cycles; i++) {
    digitalWrite(BUZZER_PIN, HIGH); delayMicroseconds(half);
    digitalWrite(BUZZER_PIN, LOW);  delayMicroseconds(half);
  }
}

// ── Navigation beeps ──────────────────────────────────────────
void beepRight()    { tone_(2500, 200); }
void beepLeft()     { tone_(800,  200); }
void beepStraight() { tone_(1500, 150); delay(120); tone_(1500, 150); }
void beepUTurn()    {
  tone_(800,120); delay(80); tone_(2500,120);
  delay(80); tone_(800,120); delay(80); tone_(2500,120);
}
void beepArrived()  {
  tone_(1000,100); delay(60); tone_(1500,100); delay(60);
  tone_(2000,100); delay(60); tone_(2500,100); delay(60); tone_(3000,300);
}
void beepStop()     { tone_(600, 800); }

// ── Obstacle beeps (different tones from nav) ─────────────────
void beepNotice()   { tone_(1000, 80); }
void beepWarning()  { tone_(1800,120); delay(80); tone_(1800,120); }
void beepDanger()   { for(int i=0;i<5;i++){ tone_(3000,80); delay(60); } }

// ── System beeps ──────────────────────────────────────────────
void beepSOS() {
  // Morse · · · — — — · · ·
  for(int i=0;i<3;i++){ tone_(1800,120); delay(100); }
  delay(150);
  for(int i=0;i<3;i++){ tone_(1800,350); delay(120); }
  delay(150);
  for(int i=0;i<3;i++){ tone_(1800,120); delay(100); }
}
void beepBoot() {
  tone_(1200,150); delay(80); tone_(1600,150); delay(80); tone_(2000,200);
}
void beepConn() { tone_(1500,100); delay(80); tone_(1500,100); }

// ============================================================
//  ULTRASONIC
// ============================================================
float readDistance() {
  digitalWrite(TRIG_PIN, LOW);  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH); delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  long dur = pulseIn(ECHO_PIN, HIGH, 25000);
  if (dur == 0) return -1;
  float d = (dur * 0.0343f) / 2.0f;
  return (d <= 0 || d > 400) ? -1 : d;
}

float stableDistance() {
  float sum = 0; int ok = 0;
  for (int i = 0; i < 3; i++) {
    float d = readDistance();
    if (d > 0) { sum += d; ok++; }
    delay(10);
  }
  return ok == 0 ? -1 : sum / ok;
}

int getObsLevel(float d) {
  if (d <= 0)            return 0;
  if (d <= DIST_DANGER)  return 3;
  if (d <= DIST_WARNING) return 2;
  if (d <= DIST_NOTICE)  return 1;
  return 0;
}

// ============================================================
//  OBSTACLE HANDLER  —  runs every SENSOR_INTERVAL ms
// ============================================================
void handleObstacle() {
  unsigned long now = millis();
  if (now - lastSensorTime < SENSOR_INTERVAL) return;
  lastSensorTime = now;

  float dist  = stableDistance();
  int   level = getObsLevel(dist);

  // Buzzer — danger always beeps, others only on level change
  if      (level == 3)                             beepDanger();
  else if (level == 2 && level != lastObsLevel)    beepWarning();
  else if (level == 1 && level != lastObsLevel)    beepNotice();

  // BLE notify phone on level change only
  if (level != lastObsLevel) {
    lastObsLevel = level;
    switch(level) {
      case 0: notifyPhone("OBS:CLEAR");  break;
      case 1: notifyPhone("OBS:NOTICE"); break;
      case 2: notifyPhone("OBS:WARN");   break;
      case 3: notifyPhone("OBS:DANGER"); break;
    }
    Serial.print("[OBS] level="); Serial.print(level);
    Serial.print(" dist=");       Serial.println(dist, 1);
  }
}

// ============================================================
//  SOS BUTTON HANDLER  —  single press triggers SOS
// ============================================================
void handleButton() {
  unsigned long now = millis();

  // Ignore during cooldown
  if (sosInCooldown) {
    if (now - sosLastTrigger >= BTN_COOLDOWN) {
      sosInCooldown = false;
      Serial.println("[SOS] Ready again");
    }
    return;
  }

  bool btnDown = (digitalRead(BTN_PIN) == LOW);
  if (btnDown && !btnWasDown) {
    Serial.println("[BTN] SOS pressed!");
    sosInCooldown  = true;
    sosLastTrigger = now;
    beepSOS();
    notifyPhone("SOS");
    Serial.println("[SOS] Sent to phone — cooldown 10s");
  }
  btnWasDown = btnDown;
}

// ============================================================
//  NAVIGATION COMMAND HANDLER
// ============================================================
void handleCommand(String cmd) {
  cmd.trim();
  Serial.print("[CMD] " + cmd);
  if      (cmd == "R") { beepRight();    Serial.println(" → RIGHT");    }
  else if (cmd == "L") { beepLeft();     Serial.println(" → LEFT");     }
  else if (cmd == "S") { beepStraight(); Serial.println(" → STRAIGHT"); }
  else if (cmd == "U") { beepUTurn();    Serial.println(" → U-TURN");   }
  else if (cmd == "A") { beepArrived();  Serial.println(" → ARRIVED");  }
  else if (cmd == "X") { beepStop();     Serial.println(" → STOP");     }
  else                 { Serial.println(" → Unknown"); return; }
  notifyPhone("OK");
}

// ============================================================
//  BLE HELPERS
// ============================================================
void notifyPhone(const char* msg) {
  if (!bleConnected || !pNotifyChar) return;
  pNotifyChar->setValue(msg);
  pNotifyChar->notify();
  Serial.print("[BLE] → "); Serial.println(msg);
}
void notifyPhone(String msg) { notifyPhone(msg.c_str()); }

class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* s) override {
    bleConnected = true;
    beepConn();
    Serial.println("[BLE] Phone connected");
  }
  void onDisconnect(BLEServer* s) override {
    bleConnected = false;
    Serial.println("[BLE] Phone disconnected");
  }
};

class CmdCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* p) override {
    pendingCmd = p->getValue().c_str();
  }
};

// ============================================================
//  SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  Serial.println("\n==============================");
  Serial.println("   SmartStick Final v1.0");
  Serial.println("   TRIG=4 ECHO=5 BUZZ=21 BTN=2");
  Serial.println("==============================");

  pinMode(TRIG_PIN,   OUTPUT);
  pinMode(ECHO_PIN,   INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(BTN_PIN,    INPUT_PULLUP);
  digitalWrite(BUZZER_PIN, LOW);
  digitalWrite(TRIG_PIN,   LOW);

  beepBoot();

  BLEDevice::init(DEVICE_NAME);
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());

  BLEService* pSvc = pServer->createService(BLE_SERVICE_UUID);

  pCmdChar = pSvc->createCharacteristic(
    BLE_CMD_CHAR_UUID,
    BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR
  );
  pCmdChar->setCallbacks(new CmdCallbacks());

  pNotifyChar = pSvc->createCharacteristic(
    BLE_NOTIFY_CHAR_UUID,
    BLECharacteristic::PROPERTY_NOTIFY | BLECharacteristic::PROPERTY_READ
  );
  pNotifyChar->addDescriptor(new BLE2902());

  pSvc->start();
  BLEDevice::getAdvertising()->addServiceUUID(BLE_SERVICE_UUID);
  BLEDevice::getAdvertising()->setScanResponse(true);
  BLEDevice::startAdvertising();

  Serial.println("[BLE] Advertising as 'SmartStick'");
  Serial.println("==============================");
}

// ============================================================
//  LOOP
// ============================================================
void loop() {
  // Re-advertise after disconnect
  if (!bleConnected && wasConnected) {
    delay(300);
    BLEDevice::startAdvertising();
    Serial.println("[BLE] Re-advertising...");
  }
  wasConnected = bleConnected;

  // 1. Process nav command from phone
  if (pendingCmd.length() > 0) {
    handleCommand(pendingCmd);
    pendingCmd = "";
  }

  // 2. Ultrasonic obstacle detection (runs even without phone)
  handleObstacle();

  // 3. SOS button
  handleButton();

  delay(10);
}
