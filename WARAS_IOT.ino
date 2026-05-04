#include <WiFi.h>
#include <WiFiManager.h>
#include <FirebaseESP32.h>
#include <ModbusMaster.h>
#include <time.h>
#include <ESP32Servo.h>

// --- PANGGIL FILE FUZZY ---
#include "FuzzyControl.h" 

// --- KONFIGURASI WIFI & FIREBASE ---
#define FIREBASE_HOST "https://waras-iot-default-rtdb.asia-southeast1.firebasedatabase.app"
#define FIREBASE_AUTH "####################################"

// --- PIN DEFINITIONS (SUDAH DISESUAIKAN TUAN MUDA) ---
#define RXD2 16 // Modbus RX
#define TXD2 17 // Modbus TX
#define PH_SENSOR_PIN 34
#define DINAMO_RELAY_PIN 27  // Relay untuk Dinamo Pelontar pakan (Muter 5 detik)
#define FEEDER_SERVO_PIN 12  // Servo untuk Katup penjatuh pakan

// --- VARIABLES SENSOR ---
float calibration_value = 21.34; 
int buffer_arr[10], temp;
unsigned long int avgval;

// Timer History (1 Menit)
unsigned long lastHistorySave = 0;
const unsigned long historyInterval = 60000; 

// --- VARIABLES AUTO MODE (STATE MACHINE) ---
Servo feederServo;
bool autoModeActive = false;
unsigned long startTime = 0;
int stepAuto = 0;

// Durasi Katup Servo (Diatur otomatis oleh Fuzzy nanti)
int durasiBukaServo = 0; 

// --- VARIABLES TRIGGER FUZZY ---
unsigned long lastFeedTime = 0;
unsigned long feedCooldown = 7200000; // Jeda statis dikunci 2 jam (7200000 ms) agar tidak spam

// --- OBJECTS ---
ModbusMaster node;
FirebaseData firebaseData;
FirebaseData firebaseControl;
FirebaseConfig config;
FirebaseAuth auth;

void setup() {
  Serial.begin(115200);

  // 1. Inisialisasi Dinamo Pelontar (ACTIVE-LOW)
  pinMode(DINAMO_RELAY_PIN, OUTPUT);
  digitalWrite(DINAMO_RELAY_PIN, HIGH); // Posisi awal mati

  // 2. Inisialisasi Katup Feeder (Servo)
  feederServo.attach(FEEDER_SERVO_PIN);
  feederServo.write(48); // Posisi default: Katup Tertutup (48 derajat)

  // Inisialisasi Modbus
  Serial2.begin(4800, SERIAL_8N1, RXD2, TXD2);
  delay(2000);
  node.begin(1, Serial2);

  // Inisialisasi Otak Fuzzy
  setupFuzzy();

  // ======================================================================
  // WiFiManager Setup
  // ======================================================================
  WiFiManager wm;
  
  // wm.resetSettings(); // Buka komentar ini jika ingin reset WiFi

  bool res = wm.autoConnect("WARAS-Setup"); 
  if(!res) {
    Serial.println("Gagal terhubung. Restart dalam 5 detik...");
    delay(5000);
    ESP.restart();
  } 
  else {
    Serial.println("\nWiFi Connected!");
  }
  // ======================================================================

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
      
      // Kontrol Katup Servo Manual
      if (Firebase.getBool(firebaseControl, "/control/actuators/feeder")) {
        feederServo.write(firebaseControl.boolData() ? 0 : 48); // 0 = Buka, 48 = Tutup
      }
      // Kontrol Dinamo Pelontar Manual
      if (Firebase.getBool(firebaseControl, "/control/actuators/pelontar")) {
        digitalWrite(DINAMO_RELAY_PIN, firebaseControl.boolData() ? LOW : HIGH);
      }
    } 
    else if (mode == "otomatis" || mode == "auto") {
      
      // 1. Logika Trigger Otomatis dari Tombol Dashboard
      if (Firebase.getBool(firebaseControl, "/control/actuators/start_feed")) {
         if (firebaseControl.boolData() == true && !autoModeActive) {
            autoModeActive = true;
            stepAuto = 0;
            startTime = millis();
            durasiBukaServo = 3000; // Standar buka 3 detik kalau dipencet manual dari dashboard
            Firebase.setBool(firebaseData, "/control/actuators/start_feed", false);
         }
      }

      // 2. Logika Trigger Otomatis berdasarkan FUZZY LOGIC
      unsigned long currentMillis = millis();
      
      // Cek apakah sudah lewat 2 jam (cooldown)
      if (!autoModeActive && (lastFeedTime == 0 || (currentMillis - lastFeedTime >= feedCooldown))) {
        
        float hasilRate = 0;
        float hasilIntervalJam = 0; // Tidak dipakai karena cooldown dikunci statis

        // Panggil otak Fuzzy (Parameter: pH, DO, Suhu)
        hitungAksiFuzzy(phValue, doConcentration, temperature, hasilRate, hasilIntervalJam);

        // --- DEFUSIFIKASI KE SERVO ---
        // Asumsi Katup Servo paling lama membuka adalah 3000ms (3 detik) jika Rate 100%
        durasiBukaServo = (int)((hasilRate / 100.0) * 3000); 

        // Eksekusi Pakan!
        autoModeActive = true;
        stepAuto = 0;
        startTime = currentMillis;
        lastFeedTime = currentMillis; 

        Serial.println("=====================================");
        Serial.println("🧠 FUZZY LOGIC TRIGGERED!");
        Serial.printf("Rate Pakan: %.2f%% -> Katup Buka: %d ms\n", hasilRate, durasiBukaServo);
        Serial.println("Dinamo akan berputar 5 detik penuh.");
        Serial.println("Interval dikunci statis: 2 Jam.");
        Serial.println("=====================================");
      }
    }
  }

  // --- D. STATE MACHINE (MODE AUTO DINAMO + SERVO) ---
  if (autoModeActive) {
    unsigned long now = millis();
    unsigned long elapsedTime = now - startTime;

    switch (stepAuto) {
      case 0: // 1. Nyalakan Dinamo
        Serial.println("AUTO: Dinamo Pelontar ON (Berputar...)");
        digitalWrite(DINAMO_RELAY_PIN, LOW); // Relay Nyala
        stepAuto = 1;
        break;

      case 1: // 2. Tunggu 1 Detik agar dinamo kencang, lalu Buka Katup Servo
        if (elapsedTime >= 1000) {
          Serial.println("AUTO: Katup Feeder DIBUKA! (Pakan jatuh)");
          feederServo.write(0); // Posisi Buka
          stepAuto = 2;
        }
        break;

      case 2: // 3. Tutup Katup Servo setelah durasi Fuzzy habis
        // Waktu buka katup dihitung setelah detik ke-1
        if (elapsedTime >= (1000 + durasiBukaServo)) {
          Serial.println("AUTO: Katup Feeder DITUTUP.");
          feederServo.write(48); // Posisi Tutup
          stepAuto = 3;
        }
        break;

      case 3: // 4. Matikan Dinamo setelah berputar genap 5 Detik
        if (elapsedTime >= 2000) {
          Serial.println("AUTO: Dinamo Pelontar OFF. Siklus Pakan Selesai!");
          digitalWrite(DINAMO_RELAY_PIN, HIGH); // Relay Mati
          autoModeActive = false; // Kembalikan ke standby menunggu 2 jam
        }
        break;
    }
  }

  Serial.printf("📊 PH: %.2f | DO: %.2f | Temp: %.2f\n", phValue, doConcentration, temperature);
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
