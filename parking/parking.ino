#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <MFRC522.h>
#include <SPI.h>
#include <Wire.h>
#include <hd44780.h>
#include <hd44780ioClass/hd44780_I2Cexp.h>
#include <ESP32Servo.h>

// ==================== CẤU HÌNH ====================
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

// RFID
#define SS_PIN_RA   4
#define SS_PIN_VAO  5
#define RST_PIN_RA  25
#define RST_PIN_VAO 26
MFRC522 rfid_ra(SS_PIN_RA, RST_PIN_RA);
MFRC522 rfid_vao(SS_PIN_VAO, RST_PIN_VAO);

// Đếm xe trong bãi (dựa hoàn toàn vào IR)
int occupied = 0;
int reserved = 0;
const int TOTAL_SLOTS = 4;

// LCD + thông báo tạm
hd44780_I2Cexp lcd;
unsigned long showMessageUntil = 0;   // thời điểm hết hiển thị thông báo

// Servo
const uint8_t servoPin[2] = {17, 16};  // 17=RA, 16=VAO
Servo servo[2];

// 4 cảm biến IR hiện có (LOW = có xe)
const uint8_t irPin[4] = {13, 15, 14, 27};
int lastIrCount = 0;                  // số lượng IR bị che lần trước

// RFID buffer + chống quét trùng
String lastScannedUID = "";
unsigned long lastScanTime = 0;
char rfidBuf[32];
void show(const String &msg, uint16_t ms = 2000);
void fetchReserved() {
  if (Firebase.RTDB.getInt(&fbdo, "/parking/reserved")) {
    reserved = fbdo.to<int>();
    reserved = constrain(reserved, 0, TOTAL_SLOTS);
  }
}

// ==================== SETUP ====================
void setup() {
  Serial.begin(115200);
  Serial.println("\n=== PARKING - CHI DEM TONG (IR) ===");

  // WiFi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) delay(500);
  Serial.println("WiFi Connected!");

  // Firebase
  config.api_key = api_key;
  config.database_url = db_url;
  Firebase.signUp(&config, &auth, "", "");
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
  fetchReserved();

  // RFID
  SPI.begin();
  pinMode(RST_PIN_RA, OUTPUT);
  pinMode(RST_PIN_VAO, OUTPUT);
  digitalWrite(RST_PIN_RA, HIGH);
  digitalWrite(RST_PIN_VAO, HIGH);
  rfid_ra.PCD_Init();
  rfid_vao.PCD_Init();

  // LCD
  Wire.begin();
  lcd.begin(16, 2);
  lcd.backlight();
  lcd.print("Khoi dong...");

  // Servo
  servo[0].attach(servoPin[0]);
  servo[1].attach(servoPin[1]);
  servo[0].write(0);
  servo[1].write(0);

  // IR
  for (int i = 0; i < 4; i++) {
    pinMode(irPin[i], INPUT_PULLUP);
  }

  // Lấy giá trị occupied ban đầu từ Firebase (phòng khi reset)
  if (Firebase.RTDB.getInt(&fbdo, "/parking/occupied")) {
    occupied = fbdo.to<int>();
    occupied = constrain(occupied, 0, 4);
  }

  delay(1000);
  updateLCD();
}

// ==================== LOOP ====================
void loop() {
  if (!Firebase.ready()) return;

  // ===== ĐẾM XE BẰNG 4 CẢM BIẾN IR (chính xác nhất) =====
  int currentCount = 0;
  for (int i = 0; i < 4; i++) {
    if (digitalRead(irPin[i]) == LOW) currentCount++;   // có xe che cảm biến
  }

  if (currentCount != lastIrCount) {
    occupied = currentCount;
    Firebase.RTDB.setInt(&fbdo, "/parking/occupied", occupied);
    Serial.println("IR count -> occupied = " + String(occupied));
    lastIrCount = currentCount;
    updateLCD();
  }

  // ===== RFID CỔNG RA =====
  digitalWrite(SS_PIN_RA, LOW);
  rfid_ra.PCD_Init();
  if (rfid_ra.PICC_IsNewCardPresent() && rfid_ra.PICC_ReadCardSerial()) {
    getRFID(rfid_ra);
    String uid = String(rfidBuf);

    if (uid == lastScannedUID && millis() - lastScanTime < DEBOUNCE_DELAY) {
      rfid_ra.PICC_HaltA();
      digitalWrite(SS_PIN_RA, HIGH);
    } else {
      lastScannedUID = uid;
      lastScanTime = millis();

      if (checkUser() && isCarInParking(uid)) {
        show("CHAO TAM BIET!", 2000);
        openGate(0);
        removeCarFromParking(uid);
      } else {
        show("THE KHONG HOP LE", 2000);
      }
      rfid_ra.PICC_HaltA();
    }
  }
  digitalWrite(SS_PIN_RA, HIGH);

  // ===== RFID CỔNG VÀO =====
  digitalWrite(SS_PIN_VAO, LOW);
  rfid_vao.PCD_Init();
  if (rfid_vao.PICC_IsNewCardPresent() && rfid_vao.PICC_ReadCardSerial()) {
    getRFID(rfid_vao);
    String uid = String(rfidBuf);

    if (uid == lastScannedUID && millis() - lastScanTime < DEBOUNCE_DELAY) {
      rfid_vao.PICC_HaltA();
      digitalWrite(SS_PIN_VAO, HIGH);
    } else {
      lastScannedUID = uid;
      lastScanTime = millis();

      if (!checkUser()) {
        show("THE KHONG HOP LE", 2000);
      } else if (occupied >= TOTAL_SLOTS) {
        show("BAI DA DAY!", 3000);
      } else {
        show("CHAO MUNG VAO!", 2000);
        openGate(1);
        addCarToParking(uid);
      }
      rfid_vao.PICC_HaltA();
    }
  }
  digitalWrite(SS_PIN_VAO, HIGH);

  // Cập nhật LCD định kỳ
  updateLCD();
  delay(50);
}

// ==================== CÁC HÀM HỖ TRỢ ====================
// Hiển thị thông báo tạm thời
void show(const String &msg, uint16_t ms) {
  showMessageUntil = millis() + ms;
  lcd.clear();
  lcd.setCursor(0, 0);
  String line1 = msg.substring(0, 16);
  lcd.print(line1);
  if (msg.length() > 16) {
    lcd.setCursor(0, 1);
    lcd.print(msg.substring(16, 32));
  }
  Serial.println("[LCD] " + msg);
}

// Cập nhật LCD (trừ khi đang hiển thị thông báo)
void updateLCD() {
  if (millis() < showMessageUntil) return;

  static unsigned long last = 0;
  if (millis() - last < 1000) return;
  last = millis();

  // Lấy lại reserved từ Firebase mỗi giây
  fetchReserved();

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("XE:");
  lcd.print(occupied);
  lcd.print("/");
  lcd.print(TOTAL_SLOTS);

  lcd.setCursor(0, 1);
  lcd.print("DAT TRUOC:");
  lcd.print(reserved);
}


// Mở cổng
void openGate(uint8_t gate) {  // 0=RA, 1=VAO
  digitalWrite(gate == 0 ? RST_PIN_RA : RST_PIN_VAO, HIGH);
  servo[gate].write(90);
  delay(3000);
  servo[gate].write(0);
  digitalWrite(gate == 0 ? RST_PIN_RA : RST_PIN_VAO, LOW);
}

// Lấy UID thẻ
void getRFID(MFRC522 &rfid) {
  memset(rfidBuf, 0, sizeof(rfidBuf));
  for (byte i = 0; i < rfid.uid.size; i++) {
    if (rfid.uid.uidByte[i] < 0x10) strcat(rfidBuf, "0");
    char tmp[3];
    sprintf(tmp, "%X", rfid.uid.uidByte[i]);
    strcat(rfidBuf, tmp);
  }
}

// Kiểm tra thẻ có trong danh sách users không
bool checkUser() {
  if (!Firebase.RTDB.getJSON(&fbdo, "/users")) return false;
  FirebaseJson &json = fbdo.jsonObject();
  size_t len = json.iteratorBegin();
  for (size_t i = 0; i < len; i++) {
    String key, val; int type;
    json.iteratorGet(i, type, key, val);
    if (type == FirebaseJson::JSON_OBJECT) {
      FirebaseJsonData data;
      if (json.get(data, key + "/rfidUid") && data.to<String>() == rfidBuf) {
        json.iteratorEnd();
        return true;
      }
    }
  }
  json.iteratorEnd();
  return false;
}

// Kiểm tra xe đang trong bãi chưa
bool isCarInParking(const String& uid) {
  String path = "/parkingStatus/" + uid;
  return Firebase.RTDB.getBool(&fbdo, path) ? fbdo.to<bool>() : false;
}

// Thêm/Xóa xe khỏi parkingStatus
void addCarToParking(const String& uid) {
  Firebase.RTDB.setBool(&fbdo, "/parkingStatus/" + uid, true);
}
void removeCarFromParking(const String& uid) {
  Firebase.RTDB.deleteNode(&fbdo, "/parkingStatus/" + uid);
}