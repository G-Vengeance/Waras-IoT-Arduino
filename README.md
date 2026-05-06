# 🐟 Sistem IoT WARAS: Manajemen Akuakultur Berbasis Kecerdasan Buatan

**Sistem IoT WARAS (*Water and Ration Automation System*)** merupakan rancang bangun solusi pemantauan dan otomatisasi akuakultur yang diotaki oleh mikrokontroler **ESP32**. Sistem ini mengimplementasikan pemantauan kualitas air secara *real-time* melalui antarmuka protokol Modbus RTU dan *Realtime Database* Firebase. Lebih lanjut, sistem ini mengintegrasikan pendekatan kecerdasan buatan berbasis **Fuzzy Learning Automata (FLA)** guna menghasilkan keputusan penakaran pakan yang optimal dan presisi, serta menjaga ekuilibrium ekosistem perairan.

---

## 🚀 Spesifikasi Fungsional
*   **Otomatisasi Pakan Berbasis Kecerdasan Buatan (FLA):** Mengintegrasikan *Fuzzy Logic* sebagai dasar inferensi penakaran pakan dan *Learning Automata* sebagai mekanisme adaptasi sistem secara mandiri terhadap respons lingkungan.
*   **Mekanisme Pembelajaran Mandiri (*Reward & Penalty*):** Sistem secara berkala mengevaluasi kualitas air (khususnya tingkat Oksigen Terlarut/DO) pasca-pemberian pakan. Apabila distribusi pakan mendegradasi kualitas air, sistem akan mengaplikasikan penalti (*punishment*) pada probabilitas aksi tersebut guna mencegah repetisi pada siklus berikutnya.
*   **Kontrol Mode Ganda (*Dual Mode Control*):** Mengakomodasi intervensi manual terhadap aktuator secara *real-time* melalui *dashboard* pengguna, serta mode operasi otomatis yang sepenuhnya dikendalikan oleh algoritma kecerdasan buatan.
*   **Pemantauan Sensor Lanjutan (*Advanced Sensing*):** Akuisisi data presisi tinggi untuk parameter derajat keasaman (pH) secara analog, serta Oksigen Terlarut (DO) dan Suhu air melalui sensor standar industri berprotokol Modbus RS485.
*   **Provisi Wi-Fi Dinamis:** Implementasi portal **WiFiManager** yang memfasilitasi konfigurasi kredensial jaringan secara nirkabel (*Access Point*) tanpa memerlukan modifikasi kode sumber (*hardcoding*).
*   **Sinkronisasi Data *Real-time* dan Perekaman Deret Waktu (*Time-Series Logging*):** Integrasi Firebase untuk sinkronisasi status aktuator berlatensi rendah serta pencatatan historis data per menit yang diklasifikasikan secara sistematis berdasarkan hierarki waktu (bulan/tahun).

---

## 🛠️ Arsitektur Perangkat Keras
Arsitektur perangkat keras memisahkan modul penakar pakan dan pelontar pakan guna mitigasi kelebihan beban termal (*overheating*) pada motor berkecepatan tinggi.

| Komponen | Spesifikasi / Deskripsi | Pin ESP32 |
| :--- | :--- | :--- |
| **Mikrokontroler** | NodeMCU ESP32 DevKit V1 | - |
| **Antarmuka Modbus** | Modul RS485 to TTL (*Auto Direction*) | RX2 (16), TX2 (17) |
| **Sensor pH** | Sensor Analog pH Meter | GPIO 34 |
| **Aktuator Lontar** | Motor DC RPM Tinggi via Modul *Relay* (*Active Low*) | GPIO 27 |
| **Katup Penakar** | Motor Servo (Regulasi pembukaan saluran pakan) | GPIO 12 |

---

## 🧠 Algoritma Cerdas: Fuzzy Learning Automata (FLA)

Sistem merumuskan persentase porsi pakan (0% - 100%) melalui evaluasi matematis terhadap tiga parameter kualitas air primer.

### 1. Parameter Inferensi dan Nilai Ambang (*Fuzzy Logic*)
*   **🟢 Kondisi Optimal (Porsi Maksimal):**
    *   **Oksigen Terlarut (DO):** Konsentrasi tinggi, di atas ambang batas aman **7,0 mg/L**[cite: 8].
    *   **pH Air:** Kondisi netral dan ideal pada rentang **6,5 hingga 7,5**[cite: 9].
    *   **Suhu:** Temperatur optimal untuk metabolisme fauna akuatik pada rentang **25°C hingga 27,5°C**[cite: 1].
*   **🟡 Kondisi Waspada (Reduksi Porsi):**
    *   Terjadi apabila konsentrasi oksigen berada pada tingkat menengah (**3,0 - 7,0 mg/L**)[cite: 8], atau suhu air mengalami penurunan di bawah **25°C** maupun peningkatan melebihi **30°C**[cite: 1].
*   **🔴 Kondisi Kritis (Penghentian Pakan / 0%):**
    *   Sistem akan menginterupsi siklus pemberian pakan guna mencegah mortalitas akibat hipoksia atau toksisitas, yang terpicu apabila: DO turun secara signifikan (**<= 3,0 mg/L**)[cite: 8], atau pH mencapai level sangat asam (**<= 5,5**) maupun sangat basa (**>= 9,0**)[cite: 9].

### 2. Mekanisme Adaptasi (*Learning Automata*)
Sistem menyimpan memori komputasional untuk mengevaluasi efikasi dari siklus pakan sebelumnya:
*   Evaluasi dilakukan secara periodik setiap 2 jam terhadap fluktuasi kadar Oksigen Terlarut (DO).
*   **Penalti (*Punishment*):** Apabila terjadi penurunan DO yang tajam (selisih < -0,5 mg/L), algoritma akan mengaplikasikan penalti dengan mereduksi probabilitas pemilihan aksi pakan tersebut pada iterasi berikutnya.
*   **Penghargaan (*Reward*):** Apabila kualitas air menunjukkan tren perbaikan atau ekuilibrium yang stabil, sistem memberikan kompensasi positif berupa peningkatan probabilitas pada aksi yang dieksekusi.

---

## ⚙️ Skenario Mekanis (*Finite State Machine*)

Berdasarkan hasil defuzzifikasi persentase pakan (contoh: 100% = 5 Detik), *Finite State Machine* (FSM) mengorkestrasi sekuens mekanis untuk memastikan keandalan *hardware*:

1.  **Fase Penakaran (*Feeding*):** Motor Servo membuka katup (*valve*) untuk mendistribusikan pakan ke atas piringan lontar. Durasi bukaan bervariasi dinamis antara `0 hingga 5 detik` berdasarkan kalkulasi algoritma FLA.
2.  **Fase Penguncian (*Valve Closing*):** Setelah durasi penakaran selesai, katup ditutup rapat untuk meminimalisasi kebocoran pakan. Secara simultan, *relay* motor pelontar diaktivasi.
3.  **Fase Pelontaran (*Dispensing*):** Motor DC berputar menyemburkan pakan ke perairan dengan durasi absolut maksimal **2 detik**. Durasi ini dikalibrasi khusus untuk mitigasi risiko kelebihan panas (*overheat*) pada dinamo.
4.  **Fase Relaksasi (*Cooldown*):** Sistem bertransisi ke mode siaga (*standby*) dan menangguhkan seluruh pemicu aktuasi pakan secara statis selama **2 Jam** guna memberikan waktu peluruhan biologis pada ekosistem kolam.

---

## 💻 Struktur Pangkalan Data (*Database Structure*)
Sinkronisasi telemetri mengacu pada arsitektur Firebase *Realtime Database* berikut:
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
