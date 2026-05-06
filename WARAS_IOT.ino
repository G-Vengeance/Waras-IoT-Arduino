#include <WiFi.h>
#include <WiFiManager.h>
#include <FirebaseESP32.h>
#include <ModbusMaster.h>
#include <time.h>
#include <ESP32Servo.h>

// --- PANGGIL FILE FUZZY LEARNING AUTOMATA ---
#include "FuzzyControl.h" 

// --- KONFIGURASI WIFI & FIREBASE ---
#define FIREBASE_HOST "https://waras-iot-default-rtdb.asia-southeast1.firebasedatabase.app"
#define FIREBASE_AUTH "#############################" // database auth token

// --- PIN DEFINITIONS ---
#define RXD2 16 // Modbus RX
#define TXD2 17 // Modbus TX
#define PH_SENSOR_PIN 34
#define DINAMO_RELAY_PIN 27  // Relay untuk Dinamo Pelontar pakan (Muter 2 detik)
#define FEEDER_SERVO_PIN 12  // Servo untuk Katup penjatuh pakan (Maks 5 detik)

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

// Durasi Katup Servo (Diatur otomatis oleh Fuzzy+LA nanti, maks 5000 ms)
int durasiBukaServo = 0; 

// --- VARIABLES TRIGGER CERDAS ---
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
            durasiBukaServo = 5000; // Buka penuh 5 detik kalau dipencet manual
            Firebase.setBool(firebaseData, "/control/actuators/start_feed", false);
         }
      }

      // 2. Logika Trigger Otomatis berdasarkan FUZZY LEARNING AUTOMATA (FLA)
      unsigned long currentMillis = millis();
      
      // Cek apakah sudah lewat 2 jam (cooldown)
      if (!autoModeActive && (lastFeedTime == 0 || (currentMillis - lastFeedTime >= feedCooldown))) {
        
        // Panggil otak cerdas FLA di tab sebelah (Hanya PH, DO, dan SUHU)
        float persentasePakan = hitungAksiFLA(phValue, doConcentration, temperature);

        // Jika FLA memutuskan Stop Pakan (0%), batalkan eksekusi aktuator
        if (persentasePakan <= 0.0) {
          Serial.println("⛔ FLA MENGHENTIKAN PAKAN! Kondisi air sedang tidak layak.");
          lastFeedTime = currentMillis; // Reset timer untuk menunggu 2 jam lagi
        } 
        else {
          // --- DEFUSIFIKASI KE SERVO ---
          // Katup Servo paling lama membuka adalah 5000ms (5 detik) jika Rate 100%
          durasiBukaServo = (int)((persentasePakan / 100.0) * 5000); 

          // Eksekusi Pakan!
          autoModeActive = true;
          stepAuto = 0;
          startTime = currentMillis;
          lastFeedTime = currentMillis; 

          Serial.println("=====================================");
          Serial.println("🎯 EKSEKUSI PAKAN OLEH FLA!");
          Serial.printf("Rate Pakan Akhir : %.2f%%\n", persentasePakan);
          Serial.printf("Waktu Buka Katup : %d ms\n", durasiBukaServo);
          Serial.println("=====================================");
        }
      }
    }
  }

  // --- D. STATE MACHINE (ALUR: SERVO MAKS 5S -> DINAMO 2S) ---
  if (autoModeActive) {
    unsigned long now = millis();
    unsigned long elapsedTime = now - startTime;

    switch (stepAuto) {
      case 0: // 1. Buka Katup Servo (Pakan jatuh ke piringan dinamo)
        Serial.println("AUTO: Katup Feeder DIBUKA! (Pakan dikumpulkan...)");
        feederServo.write(0); // Posisi Buka
        startTime = now;      // Mulai hitung mundur durasi servo
        stepAuto = 1;
        break;

      case 1: // 2. Tunggu waktu Servo habis, Tutup Servo & Nyalakan Dinamo
        if (elapsedTime >= durasiBukaServo) {
          Serial.println("AUTO: Katup Feeder DITUTUP.");
          feederServo.write(48); // Posisi Tutup
          
          Serial.println("AUTO: Dinamo Pelontar ON (Menembakkan pakan...)");
          digitalWrite(DINAMO_RELAY_PIN, LOW); // Relay Dinamo Nyala
          
          startTime = now; // Mulai hitung mundur 2 detik untuk dinamo
          stepAuto = 2;
        }
        break;

      case 2: // 3. Tunggu Dinamo berputar 2 Detik, lalu Matikan
        if (elapsedTime >= 2000) {
          Serial.println("AUTO: Dinamo Pelontar OFF. Siklus Pakan Selesai!");
          digitalWrite(DINAMO_RELAY_PIN, HIGH); // Relay Dinamo Mati
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
  if(!getLocalTime(&timeinfo)) return "2026-05"; 
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
