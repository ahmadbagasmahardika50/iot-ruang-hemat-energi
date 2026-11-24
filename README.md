# iot-ruang-hemat-energi

Prototipe Sistem Monitoring dan Otomasi Ruang Kelas/Kantor Hemat Energi (Ruang C202) berbasis ESP32, sensor PIR, LDR, DHT22, protokol MQTT, dan Node-RED Dashboard (Tema C - Manajemen Energi dan Lingkungan Perkotaan).

## Arsitektur & Alur Data
`Sensor (PIR, LDR, DHT22)` $\rightarrow$ `ESP32 (FSM 3 Status, PWM Lampu, Histeresis AC)` $\rightarrow$ `MQTT (broker.hivemq.com)` $\rightarrow$ `Node-RED Dashboard & Log CSV`

- **Topik MQTT:** `kelas/uns-d3ti-kel2/c202/data` (interval publish 3 detik)
- **Simulasi Wokwi:** https://wokwi.com/projects/481026545747456001

## Pemetaan Pin (ESP32 DevKit V1)
| Komponen | Pin ESP32 | Keterangan |
| :--- | :--- | :--- |
| Sensor PIR (HC-SR501) | GPIO 13 | Deteksi gerakan penghuni |
| Modul Sensor LDR | GPIO 34 (ADC1) | Intensitas cahaya alami (lux) |
| Sensor DHT22 | GPIO 15 | Suhu (°C) dan kelembapan (%) |
| Lampu Ruang (LED PWM) | GPIO 25 | Peredupan adaptif 20%–100% |
| Modul Relay AC | GPIO 26 | Kendali beban AC (ON > 26°C, OFF ≤ 25°C) |
| Buzzer Piezo | GPIO 27 | Peringatan masa tenggang 5 menit |
| OLED SSD1306 (128×64) | GPIO 21 (SDA), GPIO 22 (SCL) | Tampilan status lokal I2C |

## Cara Menjalankan
1. Salin `firmware/secrets.example.h` menjadi `firmware/secrets.h` dan sesuaikan identitas tim/broker.
2. Buka proyek Wokwi atau unggah isi folder `firmware/` dan `wokwi/` ke simulator Wokwi lalu jalankan simulasi.
3. Impor `node-red/flows.json` ke Node-RED (`http://localhost:1880`), klik **Deploy**, lalu buka dashboard di `http://localhost:1880/ui`.
