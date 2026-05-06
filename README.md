# 🐟 WARAS IoT System: Smart Aquaculture Management

**WARAS IoT System** adalah solusi pemantauan dan otomasi tambak cerdas berbasis **ESP32**. Sistem ini tidak hanya memantau kualitas air secara *real-time* melalui protokol Modbus RTU dan Firebase, tetapi juga dilengkapi dengan kecerdasan buatan **Fuzzy Learning Automata (FLA)** untuk mengambil keputusan pemberian pakan yang paling optimal dan aman bagi ekosistem ikan.

---

## 🚀 Fitur Utama
*   **AI-Powered Auto Feeding (FLA):** Menggabungkan *Fuzzy Logic* untuk insting penakaran pakan dasar dan *Learning Automata* untuk kemampuan sistem beradaptasi (belajar) dari pengalaman secara mandiri.
*   **Self-Learning (Reward & Punishment):** Sistem akan mengevaluasi kualitas air (DO) setelah diberi makan. Jika pakan membuat air memburuk, sistem akan memberikan *Punishment* pada aksi tersebut agar tidak diulangi di masa depan.
*   **Dual Mode Control:** Mendukung kontrol **Manual** aktuator secara *real-time* via Dashboard, dan mode **Otomatis** berbasis siklus cerdas AI.
*   **Advanced Sensing:** Monitoring pH (Analog), Dissolved Oxygen (DO), dan Suhu (Sensor Industri Modbus RS485).
*   **Dynamic Wi-Fi Provisioning:** Dilengkapi portal **WiFiManager** sehingga kredensial jaringan dapat dikonfigurasi lewat *Access Point* tanpa *hardcoding*.
*   **Real-time Data Sync & Time-Series Logging:** Sinkronisasi aktuator instan via Firebase dan pencatatan riwayat data setiap 1 menit ke dalam folder cloud bulanan/tahunan secara rapi.

---

## 🛠️ Arsitektur Perangkat Keras
Sistem ini memisahkan tugas penjatuh pakan dan pelontar pakan untuk menjaga keawetan mesin (mencegah *overheat* pada dinamo RPM tinggi).

| Komponen | Deskripsi | Pin ESP32 |
| :--- | :--- | :--- |
| **Microcontroller** | ESP32 DevKit V1 | - |
| **Modbus Interface** | RS485 to TTL Adapter (Auto Direction) | RX2 (16), TX2 (17) |
| **pH Sensor** | Analog pH Sensor | GPIO 34 |
| **Dinamo Pelontar** | Dinamo RPM Tinggi via Relay (Active Low) | GPIO 27 |
| **Katup Feeder** | Servo Motor (Membuka/Menutup saluran pakan) | GPIO 12 |

---

## 🧠 Algoritma Cerdas: Fuzzy Learning Automata (FLA)

Sistem akan menentukan persentase porsi pakan (0% - 100%) dengan memantau 3 parameter utama kualitas air.

### 1. Parameter & Angka Patokan (Fuzzy Logic)
*   **🟢 Kondisi Sempurna (Porsi Maksimal):**
    *   **DO (Oksigen):** Tinggi, di atas batas aman **7.0 mg/L**[cite: 8].
    *   **pH Air:** Netral dan ideal di rentang **6.5 hingga 7.5**[cite: 9].
    *   **Suhu:** Optimal bagi pencernaan ikan di **25°C hingga 27.5°C**[cite: 1].
*   **🟡 Kondisi Waspada (Porsi Dikurangi):**
    *   Terjadi jika oksigen berada di level sedang (**3.0 - 7.0 mg/L**)[cite: 8], atau suhu air mulai mendingin di bawah **25°C** atau memanas di atas **30°C**[cite: 1].
*   **🔴 Kondisi Bahaya (Pakan STOP / 0%):**
    *   Sistem akan memblokir pemberian pakan untuk mencegah ikan mati lemas keracunan jika: Oksigen drop sangat rendah (**<= 3.0 mg/L**)[cite: 8], atau pH sangat Asam (**<= 5.5**) / sangat Basa (**>= 9.0**)[cite: 9].

### 2. Mekanisme Adaptasi (Learning Automata)
Sistem memiliki memori untuk menilai efektivitas pakan yang baru saja diberikan:
*   Setiap 2 jam sekali, sistem melihat efek pakan sebelumnya terhadap kadar Oksigen (DO).
*   **Punishment:** Jika DO anjlok drastis (selisih < -0.5 mg/L), sistem menghukum aksi tersebut dengan menurunkan peluang (probabilitas) penggunaannya di masa depan.
*   **Reward:** Jika kondisi air membaik atau stabil, sistem memberi hadiah dengan menaikkan peluang aksi tersebut.

---

## ⚙️ Skenario Mekanik (State Machine)

Setelah AI menentukan persentase pakan (Misal: 100% = 5 Detik), *State Machine* memastikan pergerakan mesin berjalan mulus dan aman bagi *hardware*:

1.  **Penakaran Pakan:** Katup Servo **DIBUKA** untuk menjatuhkan pakan ke atas piringan lontar. Durasi buka bervariasi antara `0 hingga 5 detik` murni berdasarkan kalkulasi kecerdasan buatan.
2.  **Kunci Katup:** Setelah waktu habis, Katup Servo ditutup rapat agar pakan tidak berceceran. Dinamo pelontar langsung dinyalakan.
3.  **Pelontaran Aman:** Dinamo pelontar berputar kencang menyemburkan pakan ke kolam selama maksimal **2 Detik** (untuk menjaga umur dinamo agar tidak cepat panas/jebol), lalu dimatikan.
4.  **Cooling Down:** Alat masuk ke fase hibernasi (*Cooldown*) dan tidak akan merespon pemicu pakan apa pun secara statis selama **2 Jam** penuh.

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
      "ph": 7.0,
      "temperature": 26.5,
      "timestamp": { ".sv": "timestamp" }
    },
    "history": {
      "2026-05": {
        "unique_id": { "do": 7.5, "ph": 7.0, "temperature": 26.5 }
      }
    }
  }
}
