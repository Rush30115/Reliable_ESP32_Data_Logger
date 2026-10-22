# Industrial Reliable ESP32 IoT Data Logger with AWS IoT Core (mTLS), LittleFS Store-and-Forward & Dual-Partition HTTPS OTA

An industrial-grade, fault-tolerant ESP32 IoT data logging and edge-control platform built with **ESP-IDF v5.4**, **FreeRTOS dual-core multitasking**, **Mutual TLS (mTLS) AWS IoT Core telemetry**, **CRC-16 checksum validation**, **LittleFS circular store-and-forward flash buffer**, and a **dual-partition HTTPS Over-The-Air (OTA) firmware upgrade engine** with anti-rollback safeguards.

---

## 🏗️ Architecture & Dual-Core System Design

```
                                  ESP32 DUAL-CORE PROCESSOR
          +-----------------------------------+  +-----------------------------------+
          |          CORE 1 (PRO CPU)         |  |          CORE 0 (APP CPU)         |
          |                                   |  |                                   |
          |  +-----------------------------+  |  |  +-----------------------------+  |
          |  |    sensor_task (Prio 5)     |  |  |  |    storage_task (Prio 4)    |  |
          |  |  Reads SHT40 / Sim Pressure |  |  |  | Writes to LittleFS Flash   |  |
          |  +--------------+--------------+  |  |  +-----------------------------+  |
          |  |  valve_control (Prio 5)     |  |  +-----------------------------------+  |
          |  |  GPIO 12 Actuator Threshold |  |  |    mqtt_task (Prio 3)             |  |
          |  +-----------------------------+  |  |    AWS IoT Core mTLS (Port 8883)  |  |
          +-----------------|-----------------+  |    Uplink: telemetry              |  |
                            |                    |    Downlink: commands             |  |
                    Pushes  |                    |  +-----------------------------+  |
                    32-Byte |                    |  +-----------------------------+  |
                    Struct  |                    |  |   sys_health_task (Prio 2)  |  |
                            v                    |  |   Feeds Task Watchdog TWDT  |  |
                 [ FreeRTOS xSensorQueue ] ------+->|  +-----------------------------+  |
                     (Depth: 32 items)           |  +-----------------------------+  |
                                                 |  |      ota_task (Prio 1)      |  |
                                                 |  |   Secure HTTPS Firmware DL  |  |
                                                 |  +-----------------------------+  |
          +-----------------------------------+  +-----------------------------------+
```

---

## 🚀 Key Subsystems & Features

1. **AWS IoT Core Mutual TLS (mTLS) Telemetry Engine (Port 8883)**:
   * **Zero-Trust Cryptographic Authentication**: X.509 device client certificate and RSA private key embedded directly in read-only flash.
   * **Server Verification**: Validated against Amazon Trust Services (`AmazonRootCA1.pem`).
   * **QoS 1 Telemetry Stream**: Confirmed delivery with server `PUBACK` acknowledgments.
   * **Bi-Directional Command Downlink**: Subscribes to `devices/reliable-data-logger-01/commands/#` for dynamic runtime threshold adjustments and remote OTA triggers.

2. **Dual-Core Task Isolation**:
   * **Core 1**: Real-time sensor acquisition, digital filtering, and local safety valve actuation (`GPIO 12`).
   * **Core 0**: Asynchronous network I/O, LittleFS flash operations, AWS IoT Core mTLS networking, watchdog supervision, and background OTA upgrades.

3. **Offline Store-and-Forward Engine**:
   * LittleFS persistent flash storage (`/littlefs/log_XXXX.bin`).
   * Automatic backlog draining upon Wi-Fi reconnection in strict FIFO order with zero data loss.

4. **Edge Actuation & Dynamic Threshold Control**:
   * In-firmware valve controller evaluating pressure against safety thresholds.
   * Immediate state change (OPEN/CLOSED) upon local sensor reading or remote AWS IoT downlink command.

5. **Secure Dual-Partition HTTPS OTA Upgrade Engine**:
   * **Ping-Pong Dual Slots**: Alternating firmware flashing between `ota_0` (`0x10000`) and `ota_1` (`0x190000`).
   * **Anti-Rollback Protection**: Startup self-testing via `esp_ota_mark_app_valid_cancel_rollback()`.

---

## 📡 MQTT Topic Architecture

| Direction | Topic | Payload Example | Purpose |
| :--- | :--- | :--- | :--- |
| **Uplink (Publish)** | `devices/reliable-data-logger-01/telemetry` | `{"device_id":"reliable-data-logger-01","pressure":33.06,"temperature":33.06,"humidity":46.68,"battery_mv":3300,"valve_state":1,"threshold":30.0}` | Real-time periodic telemetry |
| **Downlink (Subscribe)** | `devices/reliable-data-logger-01/commands/threshold` | `{"threshold": 45.0}` | Dynamic pressure threshold update & valve actuation |
| **Downlink (Subscribe)** | `devices/reliable-data-logger-01/commands/ota` | `{"trigger": true}` | Remote OTA upgrade trigger |

---

## 📊 Flash Memory & Partition Table (`partitions.csv`)

| Partition Label | Type | SubType | Offset | Size | Purpose |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `nvs` | `data` | `nvs` | `0x009000` | 16 KB | Non-Volatile Storage (Wi-Fi credentials, state) |
| `otadata` | `data` | `ota` | `0x00D000` | 8 KB | Active boot slot selector & rollback registry |
| `phy_init` | `data` | `phy` | `0x00F000` | 4 KB | RF calibration data |
| `ota_0` | `app` | `ota_0` | `0x010000` | 1.5 MB | Primary Application Firmware Slot |
| `ota_1` | `app` | `ota_1` | `0x190000` | 1.5 MB | Secondary Application Firmware Slot (OTA Target) |
| `storage` | `data` | `spiffs` | `0x310000` | 960 KB | LittleFS Offline Store-and-Forward Log Files |

---

## 🔌 Hardware Pin Mapping

| ESP32 Pin | Component / Function | Notes |
| :--- | :--- | :--- |
| **GPIO 12** | Valve Solenoid Control Output | Active HIGH when pressure exceeds threshold |
| **GPIO 15** | Yellow Status LED | Blinks every 1s during normal operation |
| **GPIO 21** | I2C SDA (SSD1306 OLED & SHT40) | 100 kHz I2C bus with pull-ups |
| **GPIO 22** | I2C SCL (SSD1306 OLED & SHT40) | 100 kHz I2C bus with pull-ups |
| **GPIO 0** | BOOT Button | Manual diagnostic / OTA trigger |
| **GPIO 13** | Hardware Pushbutton | Network disconnect / failover test |
| **3V3 & GND**| Power Rails | Standard 3.3V logic |

---

## 🛠️ Getting Started & Compilation

### 1. Set Up Wi-Fi Credentials
Copy the credentials template and configure your local network:
```bash
cp main/wifi_credentials.h.example main/wifi_credentials.h
```
Edit `main/wifi_credentials.h`:
```c
#define WIFI_SSID "YOUR_WIFI_SSID"
#define WIFI_PASS "YOUR_WIFI_PASSWORD"
```
*(Note: `main/wifi_credentials.h` is gitignored to prevent leaking private credentials).*

### 2. Configure AWS IoT Core Credentials
Place your AWS credentials in `main/certs/`:
1. `AmazonRootCA1.pem` (Amazon Trust Services root certificate)
2. `device.cert.pem` (Your AWS IoT device certificate)
3. `device.private.key` (Your AWS IoT device private key)

*(Note: `*.key` and `device.cert.pem` are gitignored).*

### 3. Build & Flash
Load the ESP-IDF v5.4 toolchain:
```bash
. $HOME/esp/v5.4/esp-idf/export.sh
cd ~/esp/reliable_data_logger
idf.py build
```

Flash directly to your ESP32 board over USB:
```bash
idf.py -p /dev/ttyUSB0 flash monitor
```
*(Press `Ctrl+]` to exit the serial monitor).*

---

## 🧪 Testing with AWS IoT Core

1. Open the **AWS IoT Core Console** in your project's region (`ap-southeast-2`).
2. Navigate to **MQTT test client** $\rightarrow$ **Subscribe to a topic**.
3. Subscribe to:
   ```text
   devices/reliable-data-logger-01/#
   ```
4. Watch live telemetry records roll in with confirmed delivery (`PUBACK`).
5. To test downlink valve actuation, publish to:
   * **Topic:** `devices/reliable-data-logger-01/commands/threshold`
   * **Payload:** `{"threshold": 40.0}`
   * The ESP32 will immediately log:
     `⚡ Dynamic Threshold Updated via AWS IoT: 40.00 bar` and actuate GPIO 12.
