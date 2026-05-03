#include <WiFi.h>
#include <WiFiManager.h>      // <-- Tambahkan library WiFiManager
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

// Timer History (1 Menit, sesuai permintaan Tuan Muda)
unsigned long lastHistorySave = 0;
const unsigned long historyInterval = 60000; 

// --- VARIABLES AUTO MODE (STATE MACHINE) ---
Servo pelontarServo;
bool autoModeActive = false;
unsigned long startTime = 0;

// Pengaturan Durasi Siklus Otomatis (Bisa disesuaikan Tuan Muda)
int feederDuration = 2000;    // Lama Relay Feeder menyala agar pakan turun
int pauseDuration = 1000;     // Jeda santai sebelum Servo Pelontar menembak
int pelontarDuration = 1000;  // Lama Servo menahan posisi tembak sebelum kembali

int stepAuto = 0;

// --- OBJECTS ---
ModbusMaster node;
FirebaseData firebaseData;
FirebaseData firebaseControl;
FirebaseConfig config;
FirebaseAuth auth;

void setup() {
  Serial.begin(115200);

  // 1. Inisialisasi Aktuator Relay Feeder (ACTIVE-LOW: HIGH=OFF, LOW=ON)
  pinMode(FEEDER_RELAY_PIN, OUTPUT);
  digitalWrite(FEEDER_RELAY_PIN, HIGH); // Posisi awal mati

  // 2. Inisialisasi Servo Pelontar
  pelontarServo.attach(PELONTAR_SERVO_PIN);
  pelontarServo.write(48); // Posisi default: Siaga (48 derajat)

  // 3. Inisialisasi Modbus
  Serial2.begin(4800, SERIAL_8N1, RXD2, TXD2);
  delay(2000);
  node.begin(1, Serial2);

  // ======================================================================
  // 4. KONEKSI WIFI DINAMIS DENGAN WIFIMANAGER
  // ======================================================================
  WiFiManager wm;

  // Jika ingin ESP32 "lupa" Wi-Fi setiap kali dinyalakan (untuk testing), hapus comment di bawah
  // wm.resetSettings();

  // Membuat Access Point bernama "WARAS-Setup" jika gagal terhubung
  bool res = wm.autoConnect("WARAS-Setup"); 

  if(!res) {
    Serial.println("Gagal terhubung. Restart dalam 5 detik...");
    delay(5000);
    ESP.restart();
  } 
  else {
    // Jika berhasil terhubung!
    Serial.println("\nWiFi Connected!");
    Serial.print("Alamat IP: ");
    Serial.println(WiFi.localIP());
  }
  // ======================================================================

  // 5. Sinkronisasi Waktu (NTP)
  configTime(25200, 0, "pool.ntp.org", "time.nist.gov");
  Serial.print("Syncing time...");
  struct tm timeinfo;
  int retry = 0;
  while (!getLocalTime(&timeinfo) && retry < 10) { 
    delay(500);
    Serial.print(".");
    retry++;
  }
  Serial.println("\nReady, Tuan Muda!");

  // 6. Konfigurasi Firebase
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

    if (Firebase.setJSON(firebaseData, "/sensors/current", currentData)) {
       Serial.println("📡 Live Update Berhasil!");
    }

    if (millis() - lastHistorySave >= historyInterval) {
      String historyPath = "/sensors/history/" + getYearMonth();
      if (Firebase.pushJSON(firebaseData, historyPath, currentData)) {
        lastHistorySave = millis();
        Serial.println("📂 History Saved to: " + historyPath);
      }
    }
  }

  // --- C. TERIMA KONTROL DARI FIREBASE ---
  if (Firebase.getString(firebaseControl, "/control/mode")) {
    String mode = firebaseControl.stringData();

    if (mode == "manual") {
      autoModeActive = false; // Matikan state auto jika dipindah ke manual
      
      // 1. Kontrol Feeder (Kini menggerakkan Relay di Pin 27)
      if (Firebase.getBool(firebaseControl, "/control/actuators/feeder")) {
        digitalWrite(FEEDER_RELAY_PIN, firebaseControl.boolData() ? LOW : HIGH);
      }
      
      // 2. Kontrol Pelontar (Kini menggerakkan Servo di Pin 12)
      if (Firebase.getBool(firebaseControl, "/control/actuators/pelontar")) {
        bool isPelontarOn = firebaseControl.boolData();
        pelontarServo.write(isPelontarOn ? 0 : 48); // True = Tembak (0°), False = Siaga (48°)
      }
    } 
    else if (mode == "otomatis" || mode == "auto") {
      // Logika Trigger Pakan Otomatis dari Dashboard
      if (Firebase.getBool(firebaseControl, "/control/actuators/start_feed")) {
         if (firebaseControl.boolData() == true && !autoModeActive) {
            autoModeActive = true;
            stepAuto = 0;
            startTime = millis();
            // Matikan kembali trigger di Firebase agar siklus tidak berulang terus-menerus
            Firebase.setBool(firebaseData, "/control/actuators/start_feed", false);
         }
      }
    }
  }

  // --- D. STATE MACHINE (MODE AUTO SIKLUS PAKAN) ---
  if (autoModeActive) {
    unsigned long now = millis();

    switch (stepAuto) {
      case 0: // 1. Buka Feeder (Relay menyala agar pakan turun)
        Serial.println("AUTO: Feeder (Relay) ON, pakan turun...");
        digitalWrite(FEEDER_RELAY_PIN, LOW);
        startTime = now;
        stepAuto = 1;
        break;

      case 1: // 2. Tutup Feeder (Relay mati)
        if (now - startTime >= feederDuration) {
          Serial.println("AUTO: Feeder (Relay) OFF.");
          digitalWrite(FEEDER_RELAY_PIN, HIGH);
          startTime = now;
          stepAuto = 2;
        }
        break;

      case 2: // 3. Jeda selesai, Servo Pelontar menembak pakan
        if (now - startTime >= pauseDuration) {
          Serial.println("AUTO: Pelontar (Servo) Menembak! (0°)");
          pelontarServo.write(0);
          startTime = now;
          stepAuto = 3;
        }
        break;

      case 3: // 4. Servo Pelontar kembali ke posisi awal, Siklus Selesai
        if (now - startTime >= pelontarDuration) {
          Serial.println("AUTO: Pelontar (Servo) Kembali (48°). Siklus Pakan Selesai!");
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
