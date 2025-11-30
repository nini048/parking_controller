#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <MFRC522.h>
#include <SPI.h>
#include <Wire.h>
#include <hd44780.h>
#include <hd44780ioClass/hd44780_I2Cexp.h>
#include <ESP32Servo.h>

String lastScannedUID = "";
unsigned long lastScanTime = 0;
const unsigned long DEBOUNCE_DELAY = 4000;
// WiFi
const char* ssid = "nini";
const char* password = "12346789";

// Firebase
const char* api_key = "AIzaSyAg9ZluFU8P-sMa87sippruKX8ZVLD-aCA";
const char* db_url = "https://parking-app-flutter-e7493-default-rtdb.firebaseio.com/";
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
FirebaseData stream;

// RFID
#define SS_PIN_RA 5    // SS của cổng RA
#define SS_PIN_VAO 4   // SS của cổng VAO
#define RST_PIN_RA 26
#define RST_PIN_VAO 25

MFRC522 rfid_ra(SS_PIN_RA, RST_PIN_RA);
MFRC522 rfid_vao(SS_PIN_VAO, RST_PIN_VAO);


// LCD
hd44780_I2Cexp lcd;
unsigned long showMessageUntil = 0;
String currentMessage = "";

// Servo
const uint8_t servoPin[2] = {16, 17};
Servo servo[2];

// IR Sensors
const uint8_t irPin[4] = {13, 15, 14, 27};

// Slot status
int slotStatus[4] = {0, 0, 0, 0};
// Biến toàn cục
unsigned long slotReservedTime[4] = {0, 0, 0, 0};  // Lưu thời điểm mỗi slot được đặt chỗ

char rfidBuf[32];
void updateLCD(bool force);

void setup() {
  Serial.begin(115200);
  Serial.println("\n=== ESP32 PARKING - FIXED LCD ===");

  // WiFi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) delay(500);
  Serial.println("WiFi Connected!");

  // Firebase
  config.api_key = api_key;
  config.database_url = db_url;
  if (Firebase.signUp(&config, &auth, "", "")) {
    Serial.println("Firebase Anonymous OK");
  }
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  // Stream
  if (!Firebase.RTDB.beginStream(&stream, "/slots")) {
    Serial.println("[ERROR] Stream failed: " + stream.errorReason());
    lcd.clear(); lcd.print("Stream Error"); while (1);
  } else {
    Firebase.RTDB.setStreamCallback(&stream, onSlotsChange, onStreamTimeout);
  }

  // RFID
  SPI.begin();
  pinMode(RST_PIN_RA, OUTPUT);
  pinMode(RST_PIN_VAO, OUTPUT);
  digitalWrite(RST_PIN_RA, HIGH);   // BẬT RFID RA
  digitalWrite(RST_PIN_VAO, HIGH);  // BẬT RFID VAO
  rfid_ra.PCD_Init();
  rfid_vao.PCD_Init();
  Serial.println("RFID Ready (RA: SS=5, VAO: SS=4)");

  // LCD
  Wire.begin();
  lcd.begin(16, 2);
  lcd.backlight();
  lcd.print("Initializing...");
  delay(1000);

  // Servo
servo[0].attach(servoPin[0]); // RA
  servo[1].attach(servoPin[1]); // VAO
  servo[0].write(0);
  servo[1].write(0);

  pinMode(RST_PIN_RA, OUTPUT);
  pinMode(RST_PIN_VAO, OUTPUT);
digitalWrite(RST_PIN_RA, LOW);
digitalWrite(RST_PIN_VAO, LOW);


  // IR
  for (uint8_t i = 0; i < 4; i++) {
    pinMode(irPin[i], INPUT_PULLUP);
  }

  readInitialSlots();
}
void updateLCD(bool force = false) {
  lcd.backlight();

  // HIỂN THỊ THÔNG BÁO
  if (showMessageUntil > 0 && millis() < showMessageUntil && !force) {
    return; // Chỉ hiển thị thông báo
  }

  if (showMessageUntil > 0 && millis() >= showMessageUntil) {
    showMessageUntil = 0;
    currentMessage = "";
  }

  // ÉP CẬP NHẬT → LUÔN CLEAR
  if (force) {
    lcd.clear(); // BẮT BUỘC
    lcd.setCursor(0, 0);
    lcd.print("S1 S2 S3 S4    ");
    lcd.setCursor(0, 1);
    for (int i = 0; i < 4; i++) {
      char c = (slotStatus[i] == 0) ? '.' : (slotStatus[i] == 1 ? 'X' : 'R');
      lcd.print(c); lcd.print(" ");
    }
    int empty = 0;
    for (int i = 0; i < 4; i++) if (slotStatus[i] == 0) empty++;
    lcd.setCursor(12, 1);
    lcd.print("   ");
    lcd.setCursor(12, 1);
    lcd.print(empty); lcd.print("/4");
    return;
  }

  // CẬP NHẬT BÌNH THƯỜNG
  static unsigned long lastDraw = 0;
  if (millis() - lastDraw < 1000) return;
  lastDraw = millis();

  lcd.setCursor(0, 0);
  lcd.print("S1 S2 S3 S4    ");

  lcd.setCursor(0, 1);
  for (int i = 0; i < 4; i++) {
    char c = (slotStatus[i] == 0) ? '.' : (slotStatus[i] == 1 ? 'X' : 'R');
    lcd.print(c); lcd.print(" ");
  }

  int empty = 0;
  for (int i = 0; i < 4; i++) if (slotStatus[i] == 0) empty++;
  lcd.setCursor(12, 1);
  lcd.print("   ");
  lcd.setCursor(12, 1);
  lcd.print(empty); lcd.print("/4");
}
// Kiểm tra xe có trong bãi không (true = có, false = không)
bool isCarInParking(const String& uid) {
  String path = "/parkingStatus/" + uid;
  if (Firebase.RTDB.getBool(&fbdo, path)) {
    bool inParking = fbdo.to<bool>();
    Serial.println("[PARK] " + uid + " in parking: " + (inParking ? "YES" : "NO"));
    return inParking;
  } else {
    // Nếu node không tồn tại → xe KHÔNG trong bãi
    Serial.println("[PARK] " + uid + " NOT FOUND (not in parking)");
    return false;
  }
}

// Thêm xe vào bãi (true)
void addCarToParking(const String& uid) {
  String path = "/parkingStatus/" + uid;
  if (Firebase.RTDB.setBool(&fbdo, path, true)) {
    Serial.println("[PARK] ADD " + uid + " = true");
  } else {
    Serial.println("[PARK] FAILED to add " + uid);
  }
}

// XÓA xe khỏi bãi (khi RA)
void removeCarFromParking(const String& uid) {
  String path = "/parkingStatus/" + uid;
  if (Firebase.RTDB.deleteNode(&fbdo, path)) {
    Serial.println("[PARK] REMOVED " + uid);
  } else {
    Serial.println("[PARK] FAILED to remove " + uid + ": " + fbdo.errorReason());
  }
}
void loop() {
  if (!Firebase.ready()) return;
  Firebase.RTDB.readStream(&stream);
  yield();
 // === RFID RA (CỔNG RA) ===
digitalWrite(SS_PIN_RA, LOW);
rfid_ra.PCD_Init();

if (rfid_ra.PICC_IsNewCardPresent() && rfid_ra.PICC_ReadCardSerial()) {
  getRFID(rfid_ra);
  String uid = String(rfidBuf);

  // Chống quét trùng
  if (uid == lastScannedUID && millis() - lastScanTime < DEBOUNCE_DELAY) {
    Serial.println("[RA] Ignored duplicate: " + uid);
    rfid_ra.PICC_HaltA();
    digitalWrite(SS_PIN_RA, HIGH);
  } else {
    Serial.println("[RFID RA] Detected: " + uid);
    lastScannedUID = uid;
    lastScanTime = millis();

    if (checkUser()) {
      if (isCarInParking(uid)) {
        show("RA OK", 1500);
        lcd.clear(); lcd.setCursor(0, 0); lcd.print("CONG RA MO");
        openGate(0);
        removeCarFromParking(uid);  // XÓA HOÀN TOÀN
      } else {
        show("XE KHONG TRONG BAI", 2000);
        lcd.clear(); lcd.setCursor(0, 0); lcd.print("KHONG TRONG BAI");
      }
    } else {
      show("THE KHONG HOP LE", 1500);
      lcd.clear(); lcd.setCursor(0, 0); lcd.print("THE KHONG HOP LE");
    }
    rfid_ra.PICC_HaltA();
  }
}
digitalWrite(SS_PIN_RA, HIGH);

// === RFID VAO (CỔNG VÀO) ===
digitalWrite(SS_PIN_VAO, LOW);
rfid_vao.PCD_Init();

if (rfid_vao.PICC_IsNewCardPresent() && rfid_vao.PICC_ReadCardSerial()) {
  getRFID(rfid_vao);
  String uid = String(rfidBuf);

  if (uid == lastScannedUID && millis() - lastScanTime < DEBOUNCE_DELAY) {
    Serial.println("[VAO] Ignored duplicate: " + uid);
    rfid_vao.PICC_HaltA();
    digitalWrite(SS_PIN_VAO, HIGH);
  } else {
    Serial.println("[RFID VAO] Detected: " + uid);
    lastScannedUID = uid;
    lastScanTime = millis();

    if (checkUser()) {
      int emptySlots = 0;
      for (int i = 0; i < 4; i++) if (slotStatus[i] == 0) emptySlots++;

      if (emptySlots > 0 || isCarInParking(uid)) {  // Cho phép vào lại nếu đã có trong bãi
        show("VAO OK", 1500);
        lcd.clear(); lcd.setCursor(0, 0); lcd.print("CONG VAO MO");
        openGate(1);
        addCarToParking(uid);  // THÊM VÀO BÃI
      } else {
        show("KHONG CON CHO", 2000);
        lcd.clear(); lcd.setCursor(0, 0); lcd.print("KHONG CON CHO");
      }
    } else {
      show("THE KHONG HOP LE", 1500);
      lcd.clear(); lcd.setCursor(0, 0); lcd.print("THE KHONG HOP LE");
    }
    rfid_vao.PICC_HaltA();
  }
}
digitalWrite(SS_PIN_VAO, HIGH);
//ir
  for (uint8_t i = 0; i < 4; i++) {
  bool current = (digitalRead(irPin[i]) == LOW);
  delay(10);
  bool confirm = (digitalRead(irPin[i]) == LOW);

  if (current == confirm) {  // Chỉ khi tín hiệu ổn định
    // Nếu slot đang được đặt trước (2) mà cảm biến phát hiện có xe → chuyển sang 3
    if (slotStatus[i] == 2 && current) {
      slotStatus[i] = 3;
      setSlot(i + 1, 3); // Gửi lên Firebase
      show("SLOT " + String(i + 1) + " CHO QUET", 2000);
      continue; // Bỏ qua kiểm tra tiếp theo
    }

    // Nếu slot đang là 3 (chờ quét mã) mà cảm biến mất tín hiệu → xe rời đi → về 2 (vẫn giữ đặt trước)
    if (slotStatus[i] == 3 && !current) {
      slotStatus[i] = 2;
      setSlot(i + 1, 2);
      show("SLOT " + String(i + 1) + " DAT LAI", 2000);
      continue;
    }

    // Xử lý các slot bình thường
    if (slotStatus[i] != 2 && slotStatus[i] != 3) {
      int newStatus = current ? 1 : 0;
      if (slotStatus[i] != newStatus) {
        slotStatus[i] = newStatus;
        setSlot(i + 1, newStatus);
      }
    }
  }
}


  updateLCD();
  delay(50);
}
// MỞ CỔNG
void openGate(uint8_t gate) {
  Serial.printf("[GATE] Opening gate %s\n", gate == 0 ? "RA" : "VAO");
  
  // Bật quạt
  digitalWrite(gate == 0 ? RST_PIN_RA : RST_PIN_VAO, HIGH);
  
  // Mở servo
  servo[gate].write(90);
  delay(3000);
  servo[gate].write(0);
  
  // Tắt quạt
  digitalWrite(gate == 0 ? RST_PIN_RA : RST_PIN_VAO, LOW);
}
void readInitialSlots() {
  if (Firebase.RTDB.getJSON(&fbdo, "/slots")) {
    FirebaseJson &json = fbdo.jsonObject();
    for (int i = 0; i < 4; i++) {
      String key = "slot" + String(i + 1);
      FirebaseJsonData result;
      if (json.get(result, key + "/status")) {
        slotStatus[i] = result.to<int>();
      }
    }
  }
  updateLCD();
}

void setSlot(uint8_t n, int status) {
  String path = "/slots/slot" + String(n);
  Firebase.RTDB.setInt(&fbdo, path + "/status", status);
}

int parseSlotIndexFromPath(const String &path) {
  // Xử lý cả "/slot1/status" và "/slot1"
  int slotPos = path.indexOf("slot");
  if (slotPos == -1) return -1;
  
  int endPos = path.indexOf("/", slotPos + 4);
  if (endPos == -1) endPos = path.length();
  
  String slotStr = path.substring(slotPos + 4, endPos);
  int num = slotStr.toInt();
  return (num >= 1 && num <= 4) ? num - 1 : -1;
}

void onSlotsChange(FirebaseStream data) {
  Serial.println("[STREAM] Data changed:");
  Serial.print("[PATH] "); Serial.println(data.dataPath());
  Serial.print("[TYPE] "); Serial.println(data.dataType());
  Serial.print("[RAW] "); Serial.println(data.jsonString());

  // === Nếu là JSON đầy đủ ===
  if (data.dataPath() == "/") {
    FirebaseJson json = data.jsonObject();
    for (int i = 0; i < 4; i++) {
      String key = "slot" + String(i + 1);
      FirebaseJsonData result;
      if (json.get(result, key + "/status")) {
        int newStatus = result.to<int>();
        if (slotStatus[i] != newStatus) {
          slotStatus[i] = newStatus;
          if (newStatus == 2) slotReservedTime[i] = millis();
          updateLCD(true);
          show("SLOT " + String(i + 1) + " DAT TRUOC", 2000);
        }
      }
    }
    return;
  }

  // === Nếu là JSON của từng slot riêng lẻ ===
  if (data.dataTypeEnum() == fb_esp_rtdb_data_type_json) {
    int idx = parseSlotIndexFromPath(data.dataPath()); // ví dụ /slot4 → 3
    if (idx >= 0 && idx < 4) {
      FirebaseJson json = data.jsonObject();
      FirebaseJsonData result;
      if (json.get(result, "status")) {
        int newStatus = result.to<int>();
        if (slotStatus[idx] != newStatus) {
          slotStatus[idx] = newStatus;
          if (newStatus == 2) slotReservedTime[idx] = millis();
          updateLCD(true);
          show("SLOT " + String(idx + 1) + " DAT TRUOC", 2000);
        }
      }
    }
    return;
  }

  // === Nếu là trường con như /slot4/status ===
  int idx = parseSlotIndexFromPath(data.dataPath());
  if (idx >= 0 && idx < 4) {
    int newStatus = data.intData();
    slotStatus[idx] = newStatus;
    updateLCD(true);
    if (newStatus == 2) slotReservedTime[idx] = millis();
    show("SLOT " + String(idx + 1) + " DAT TRUOC", 2000);
  }
}

void onStreamTimeout(bool timeout) {
  if (timeout) Serial.println("[STREAM] Timeout");
}

void show(const String &msg, uint16_t ms) {
  currentMessage = msg;
  showMessageUntil = millis() + ms;
  Serial.println("[LCD] " + msg);

  // IN NGAY + ÉP CẬP NHẬT
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(msg);
  updateLCD(true); // ÉP CẬP NHẬT SAU KHI IN
}


void getRFID(MFRC522 &rfid) {
  memset(rfidBuf, 0, sizeof(rfidBuf));
  for (byte i = 0; i < rfid.uid.size; i++) {
    if (rfid.uid.uidByte[i] < 0x10) strcat(rfidBuf, "0");
    char hex[3]; sprintf(hex, "%X", rfid.uid.uidByte[i]);
    strcat(rfidBuf, hex);
  }
  Serial.print("[RFID] UID: "); Serial.println(rfidBuf);
}

bool checkUser() {
  if (!Firebase.RTDB.getJSON(&fbdo, "/users")) {
    Serial.println("Failed to get /users: " + fbdo.errorReason());
    return false;
  }

  FirebaseJson &json = fbdo.jsonObject();
  size_t len = json.iteratorBegin();
  
  for (size_t i = 0; i < len; i++) {
    String key, val;
    int type;
    json.iteratorGet(i, type, key, val);

    if (type == FirebaseJson::JSON_OBJECT) {
      FirebaseJsonData childData;
      // Lấy rfidUid từ child object (không gọi getString!)
      if (json.get(childData, key + "/rfidUid")) {
        String uid = childData.to<String>();
        if (uid == rfidBuf) {
          Serial.println("RFID MATCH: " + uid);
          json.iteratorEnd();
          return true;
        }
      }
    }
  }
  json.iteratorEnd();
  Serial.println("RFID NOT FOUND: " + String(rfidBuf));
  return false;
}