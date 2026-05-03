# 🐟 WARAS IoT System: Smart Aquaculture Management

**WARAS IoT System** adalah solusi pemantauan dan otomasi tambak berbasis **ESP32** yang dirancang untuk menjaga kualitas air dan efisiensi pemberian pakan secara real-time. Sistem ini mengintegrasikan sensor industri melalui protokol Modbus RTU dan sinkronisasi data cloud via Firebase.

---

## 🚀 Fitur Utama
*   **Dual Mode Control:** Mendukung mode **Manual** (via Dashboard) dan mode **Otomatis** (Cycle-based feeding).
*   **Advanced Sensing:** Monitoring pH (Analog), Dissolved Oxygen (DO), dan Suhu (Modbus RTU).
*   **Dynamic Wi-Fi Provisioning:** Menggunakan **WiFiManager** sehingga kredensial Wi-Fi dapat diatur melalui portal AP tanpa *hardcoding*.
*   **Real-time Data Sync:** Integrasi Firebase untuk pembaruan status aktuator dan pembacaan sensor secara instan.
*   **Time-Series Logging:** Pencatatan riwayat data setiap 1 menit yang diorganisir otomatis berdasarkan folder bulan/tahun.
*   **Smart Feeding Sequence:** Implementasi *State Machine* untuk sinkronisasi presisi antara *Feeder Relay* dan *Servo Pelontar*.

---

## 🛠️ Arsitektur Perangkat Keras
Sistem ini menggunakan komponen dengan spesifikasi berikut:

| Komponen | Deskripsi | Pin ESP32 |
| :--- | :--- | :--- |
| **Microcontroller** | ESP32 DevKit V1 | - |
| **Modbus Interface** | RS485 to TTL Adapter | RX2 (16), TX2 (17) |
| **pH Sensor** | Analog pH Sensor | GPIO 34 |
| **Relay Feeder** | Module Relay (Active Low) | GPIO 27 |
| **Servo Pelontar** | MG996R / SG90 | GPIO 12 |

---

## 💻 Struktur Database Firebase
Sistem bekerja dengan struktur **Realtime Database** sebagai berikut:
```json
{
  "control": {
    "mode": "manual/otomatis",
    "actuators": {
      "feeder": false,
      "pelontar": false,
      "start_feed": false
    }
  },
  "sensors": {
    "current": {
      "do": 7.5,
      "ph": 8.0,
      "temperature": 28.5,
      "timestamp": { ".sv": "timestamp" }
    },
    "history": {
      "2026-05": {
        "unique_id": { "do": 7.5, "ph": 8.0, "temperature": 28.5 }
      }
    }
  }
}
