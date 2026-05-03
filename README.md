#include <WiFi.h>
#include <WiFiManager.h>
#include <FirebaseESP32.h>
#include <ModbusMaster.h>
#include <time.h>
#include <ESP32Servo.h>

// --- KONFIGURASI WIFI & FIREBASE ---
#define FIREBASE_HOST "https://waras-iot-default-rtdb.asia-southeast1.firebasedatabase.app"
#define FIREBASE_AUTH "UkWDgNUkux87bEuWyWcqF6Q9voKwTLck3YCL9n8G"

// --- PIN DEFINITIONS ---
#define RXD2 16 // Modbus RX
#define TXD2 17 // Modbus TX
#define PH_SENSOR_PIN 34
#define FEEDER_RELAY_PIN 27  // Relay untuk Feeder (Pakan Jatuh)
#define PELONTAR_SERVO_PIN 12 // Servo untuk Pelontar (Penembak Pakan)

// --- VARIABLES SENSOR ---
float calibration_value = 21.34; 
int buffer_arr[10], temp;
unsigned long int avgval;

// Timer History (1 Menit)
unsigned long lastHistorySave = 0;
const unsigned long historyInterval = 60000; 

// --- VARIABLES AUTO MODE (STATE MACHINE) ---
Servo pelontarServo;
bool autoModeActive = false;
unsigned long startTime = 0;
int stepAuto = 0;

// Pengaturan Durasi Siklus Otomatis
int feederDuration = 3000;    // 1. Lama Relay Feeder menyala (3 Detik)
int pauseDuration = 2000;     // 2. Jeda waktu SETELAH feeder mati, sebelum pelontar menembak (2 detik)
int pelontarDuration = 1000;  // 3. Lama Servo menahan posisi tembak sebelum kembali (1 detik)

// --- VARIABLES TRIGGER DO ---
float doThreshold = 0; // Ambang batas DO (Misal: Pakan keluar jika DO >= 5.0)
unsigned long lastDOFeedTime = 0;
const unsigned long doFeedCooldown = 3600000; // Cooldown 1 jam (dalam milidetik) agar tidak spam pakan '7200000' untuk 2 jam delay

// --- OBJECTS ---
ModbusMaster node;
FirebaseData firebaseData;
FirebaseData firebaseControl;
FirebaseConfig config;
FirebaseAuth auth;

void setup() {
  Serial.begin(115200);

  // Inisialisasi Aktuator Relay Feeder (ACTIVE-LOW)
  pinMode(FEEDER_RELAY_PIN, OUTPUT);
  digitalWrite(FEEDER_RELAY_PIN, HIGH); // Posisi awal mati

  // Inisialisasi Servo Pelontar
  pelontarServo.attach(PELONTAR_SERVO_PIN);
  pelontarServo.write(48); // Posisi default: Siaga (48 derajat)

  // Inisialisasi Modbus
  Serial2.begin(4800, SERIAL_8N1, RXD2, TXD2);
  delay(2000);
  node.begin(1, Serial2);

  // WiFiManager Setup
  WiFiManager wm;
  bool res = wm.autoConnect("WARAS-Setup"); 
  if(!res) {
    Serial.println("Gagal terhubung. Restart dalam 5 detik...");
    delay(5000);
    ESP.restart();
  } 
  else {
    Serial.println("\nWiFi Connected!");
  }

  // Sinkronisasi Waktu (NTP)
  configTime(25200, 0, "pool.ntp.org", "time.nist.gov");
  struct tm timeinfo;
  int retry = 0;
  while (!getLocalTime(&timeinfo) && retry < 10) { 
    delay(500);
    retry++;
  }

  // Konfigurasi Firebase
  config.host = FIREBASE_HOST;
  config.signer.tokens.legacy_token = FIREBASE_AUTH;
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  Serial.println("WARAS IoT System Ready, Tuan Muda!");
}

void loop() {
  // --- A. PEMBACAAN SENSOR ---
  float doConcentration = readModbusFloat(0x0002);
  float temperature = readModbusFloat(0x0004);
  float phValue = readPH();
  
  if (doConcentration < -900) doConcentration = 0;
  if (temperature < -900) temperature = 0;

  // --- B. KIRIM DATA KE FIREBASE ---
  if (WiFi.status() == WL_CONNECTED) {
    FirebaseJson currentData;
    currentData.set("do", doConcentration);
    currentData.set("ph", phValue);
    currentData.set("temperature", temperature);
    currentData.set("timestamp/.sv", "timestamp"); 

    Firebase.setJSON(firebaseData, "/sensors/current", currentData);

    if (millis() - lastHistorySave >= historyInterval) {
      String historyPath = "/sensors/history/" + getYearMonth();
      if (Firebase.pushJSON(firebaseData, historyPath, currentData)) {
        lastHistorySave = millis();
      }
    }
  }

  // --- C. TERIMA KONTROL DARI FIREBASE & LOGIKA TRIGGER ---
  if (Firebase.getString(firebaseControl, "/control/mode")) {
    String mode = firebaseControl.stringData();

    if (mode == "manual") {
      autoModeActive = false; // Matikan state auto
      
      // Kontrol Feeder Manual
      if (Firebase.getBool(firebaseControl, "/control/actuators/feeder")) {
        digitalWrite(FEEDER_RELAY_PIN, firebaseControl.boolData() ? LOW : HIGH);
      }
      // Kontrol Pelontar Manual
      if (Firebase.getBool(firebaseControl, "/control/actuators/pelontar")) {
        pelontarServo.write(firebaseControl.boolData() ? 0 : 48);
      }
    } 
    else if (mode == "otomatis" || mode == "auto") {
      
      // 1. Logika Trigger Otomatis dari Tombol Dashboard
      if (Firebase.getBool(firebaseControl, "/control/actuators/start_feed")) {
         if (firebaseControl.boolData() == true && !autoModeActive) {
            autoModeActive = true;
            stepAuto = 0;
            startTime = millis();
            Firebase.setBool(firebaseData, "/control/actuators/start_feed", false);
         }
      }

      // 2. Logika Trigger Otomatis berdasarkan SENSOR DO
      // Membaca threshold dari sensor dan mengecek apakah cooldown sudah lewat
      if (doConcentration >= doThreshold && !autoModeActive) {
        unsigned long currentMillis = millis();
        // Cek apakah ini pertama kali jalan (last == 0) atau sudah melewati waktu cooldown
        if (lastDOFeedTime == 0 || (currentMillis - lastDOFeedTime >= doFeedCooldown)) {
          autoModeActive = true;
          stepAuto = 0;
          startTime = currentMillis;
          lastDOFeedTime = currentMillis; // Catat waktu pemberian pakan terakhir
          Serial.println("⚠️ Ambang batas DO tercapai! Pemicu pakan otomatis aktif.");
        }
      }
    }
  }

  // --- D. STATE MACHINE (MODE AUTO SIKLUS PAKAN) ---
  if (autoModeActive) {
    unsigned long now = millis();

    switch (stepAuto) {
      case 0: // 1. Buka Feeder 
        Serial.println("AUTO: Feeder ON (Pakan turun...)");
        digitalWrite(FEEDER_RELAY_PIN, LOW); // Relay Nyala
        startTime = now; // Mulai hitung waktu feeder
        stepAuto = 1;
        break;

      case 1: // 2. Tunggu 3 Detik, lalu Tutup Feeder
        if (now - startTime >= feederDuration) {
          Serial.println("AUTO: Feeder OFF. Memulai hitung mundur jeda...");
          digitalWrite(FEEDER_RELAY_PIN, HIGH); // Relay Mati
          startTime = now; // Mulai hitung waktu jeda (Dihitung SETELAH mati)
          stepAuto = 2;
        }
        break;

      case 2: // 3. Tunggu Jeda Selesai, lalu Servo Menembak
        if (now - startTime >= pauseDuration) {
          Serial.println("AUTO: Jeda selesai, Pelontar Menembak!");
          pelontarServo.write(0);
          startTime = now; // Mulai hitung waktu pelontar menahan posisi
          stepAuto = 3;
        }
        break;

      case 3: // 4. Servo Kembali, Siklus Selesai
        if (now - startTime >= pelontarDuration) {
          Serial.println("AUTO: Pelontar kembali. Siklus Selesai!");
          pelontarServo.write(48);
          autoModeActive = false; // Kembalikan ke standby
        }
        break;
    }
  }

  Serial.printf("📊 PH: %.2f | DO: %.2f | Temp: %.2f | Mode: %s\n", phValue, doConcentration, temperature, firebaseControl.stringData().c_str());
  delay(1000); 
}

// --- FUNCTIONS ---
String getYearMonth() {
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)) return "2026-04"; 
  char buf[10];
  strftime(buf, sizeof(buf), "%Y-%m", &timeinfo);
  return String(buf);
}

float readModbusFloat(uint16_t reg) {
  uint8_t result = node.readHoldingRegisters(reg, 2);
  if (result == node.ku8MBSuccess) {
    uint16_t h = node.getResponseBuffer(0);
    uint16_t l = node.getResponseBuffer(1);
    union { uint32_t i; float f; } conv;
    conv.i = ((uint32_t)h << 16) | l;
    return conv.f;
  }
  return -999.9;
}

float readPH() {
  for (int i = 0; i < 10; i++) { buffer_arr[i] = analogRead(PH_SENSOR_PIN); delay(10); }
  for (int i = 0; i < 9; i++) {
    for (int j = i + 1; j < 10; j++) {
      if (buffer_arr[i] > buffer_arr[j]) { temp = buffer_arr[i]; buffer_arr[i] = buffer_arr[j]; buffer_arr[j] = temp; }
    }
  }
  avgval = 0;
  for (int i = 2; i < 8; i++) avgval += buffer_arr[i];
  if (avgval < 150) return 0.00;
  float voltage = (float)avgval * 3.3 / 4095.0 / 6;
  float ph = -5.70 * voltage + calibration_value;
  return (ph < 0) ? 0 : (ph > 14) ? 14 : ph;
}
