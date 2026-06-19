# IoT-Based Smart Poultry Automation and Environmental Monitoring System

An automated management and telemetry system designed to control poultry pen environments and feeding schedules. This repository uses a unified **Monorepo Architecture**, housing both the low-level microcontroller firmware and the high-level companion web application dashboard in one place.

---

## 📂 Repository Architecture

The project is split into two clean main directories to keep the hardware code and web code separate:

```text
poultry-management-system/
├── .gitignore                # Filters out compiled binaries and private keys
├── README.md                 # Project master documentation (This file)
├── firmware/                 # Embedded Hardware Subsystem (PlatformIO Project)
│   ├── platformio.ini        # Build environments and library dependencies
│   └── src/
│       ├── main.cpp          # Execution logic, non-blocking network routines & timers
│       ├── config.h.example  # Public configuration template for open-source users
│       └── config.h          # LOCAL ONLY (Private Wi-Fi/MQTT credentials - hidden from Git)
└── web-app/                  # Companion Web Application (Frontend Dashboard)
    ├── index.html            # Core structural dashboard layout
    ├── styles.css            # Custom layout aesthetics
    └── app.js                # MQTT Broker WebSocket event handlers

```

---

## 🚀 Key Technical Features

* **Reliable Scheduled Feeding (RTC DS3231):** The system uses a dedicated hardware real-time clock over I2C to trigger exact 8:00 AM and 5:00 PM feeding times. Because it relies on hardware time rather than internet time (NTP), the feeding schedule will never fail, even if the internet drops for weeks.
* **Power-Loss Protection (Flash Memory API):** To prevent overfeeding, the code uses the ESP32’s permanent internal flash memory (`Preferences.h`) to log the exact day and hour of the last successful feed. If the system undergoes a sudden power reset during an active feeding window, it checks the flash memory on boot to make sure it doesn't double-feed the birds.
* **Non-Blocking Network Recovery:** Instead of using standard blocking loops (like `delay()`) that freeze the microcontroller when Wi-Fi is lost, this code uses non-blocking `millis()` timers. If the router disconnects, the ESP32 safely hunts for the network in the background while the physical sensors, servo, and lighting loops keep running offline without a single skip.
* **Dual-Mode Manual Overrides:** Built-in MQTT callback functions listen for incoming dashboard commands. You can tap a button on your phone to trigger a 2-second manual feeding test, or flip the heating bulb between `ON`, `OFF`, and `AUTO` clock schedules for maintenance.
* **Relay Startup Protection (High-Impedance Fix):** Active-LOW relay modules are prone to "flickering" during the first few milliseconds of a microcontroller's boot sequence due to internal pin fluctuations. The code solves this hardware glitch programmatically by forcing the relay GPIO pin into a high-impedance state (`INPUT`) immediately on boot, keeping the high-wattage heating bulbs safely completely isolated and off until the system is stable.

---

## 🛠️ Hardware Requirements

* **Main Controller:** ESP32-C3 Pro Mini (SuperMini Development Board)
* **Timekeeper Module:** DS3231 Precision I2C Real-Time Clock (+ CR2032 backup battery)
* **Microclimate Sensor:** SHTC3 Ambient Temperature & Relative Humidity Sensor
* **Silo & Tank Indicators:** Dual HC-SR04 Ultrasonic Transducers (Water depth & Feed hopper measurements)
* **Actuators:** High-Torque Mechanical Servo Motor (Feed gate), 5V Optocoupler-Isolated Relay (Heating element)

---

## 💻 Firmware Installation & Setup

The hardware project is written in C++ using the **PlatformIO** framework inside VS Code.

### 1. Local Network Provisioning

For security compliance, the repository completely strips personal credentials from tracking. To run this project locally, you must provide your own network configuration file:

1. Navigate to `firmware/src/`
2. Duplicate `config.h.example` and rename the copy to `config.h`
3. Open `config.h` and insert your specific network configurations:

```cpp
// Configuration snippet inside firmware/src/config.h
const char* ssid     = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";
const char* mqtt_server = "YOUR_SECURE_MQTT_ BROKER_CLUSTER_ENDPOINT";
const int   mqtt_port   = 8883; // Secure TLS Port

```

### 2. Compile and Deploy

Connect the ESP32-C3 board to your machine via a data-capable USB-C link and use the PlatformIO CLI or toolbar parameters:

```bash
# Move into the embedded directory
cd firmware

# Compile library binaries and build the execution images
pio run

# Flash the compiled code directly onto the ESP32-C3
pio run --target upload

# Open the local serial logging terminal (Set at 115200 Baud)
pio device monitor

```

---
---

## 📱 Temporary HMI Mobile App Setup

Pending the full deployment of the custom companion web application in the `web-app/` directory, the system currently uses the **IoT MQTT Panel** mobile application (or any standard MQTT client) for live testing, telemetry display, and manual hardware overrides.

### Connection Configuration:
1. **Broker Web-Socket / MQTT Endpoint:** Use the secure cluster URL provided in `config.h`.
2. **Port:** `8883` (SSL/TLS secure connection).
3. **Authentication:** Enter your MQTT Broker database username and password.

To build the dashboard interface on your phone, create widgets (Gauges, Text Displays, and Buttons) and map them to the corresponding topics in the **MQTT Telemetry Matrix** below.

## 📊 MQTT Telemetry & Messaging Architecture

Both the hardware firmware and the frontend application communicate with each other across a unified MQTT publishing matrix.

### Telemetry Reporting Channels (From ESP32 to Dashboard)

| Target Publish Topic | Data Payload Schema | Telemetry Description |
| --- | --- | --- |
| `poultry/status` | String (`Online` / `Offline`) | Node Availability Status (Last Will and Testament Message) |
| `poultry/temp` | Float (e.g., `28.45`) | Live ambient pen temperature monitoring (°C) |
| `poultry/hum` | Float (e.g., `62.10`) | Live ambient relative humidity monitoring (%) |
| `poultry/waterLevel` | Integer (`0` to `100`) | Mapped ultrasonic capacity representation of water tank (%) |
| `poultry/feedLevel` | Integer (`0` to `100`) | Mapped ultrasonic capacity representation of feed hopper (%) |
| `poultry/currentTime` | String (`HH:MM:SS`) | Continuous internal hardware clock verification output |
| `poultry/lastFed` | String (`HH:MM:SS`) | Flash-persistent record of the last successful feeding cycle |

### Remote Command Channels (From Dashboard to ESP32)

#### 1. Mechanical Feeder Gate Command

* **Topic:** `poultry/cmd/feed`
* **Valid Payload:** `TRIGGER`
* **System Action:** Bypasses scheduled constraints to open the feed door for exactly 2 seconds. The closure loop automatically resets variables, writes to Flash, and flags `poultry/lastFed` when finished.

#### 2. Heating Lamp System Command

* **Topic:** `poultry/cmd/bulb`
* **Payload Choices:**

| Received Payload | System Hardware Operational Response |
| --- | --- |
| `AUTO` | Hands logic back to the RTC scheduling algorithm (Bulb ON between 7 PM - 6 AM). |
| `ON` | Overrides scheduling; immediately latches the relay close path to power the bulb. |
| `OFF` | Overrides scheduling; flips the GPIO pin to high-impedance mode to break the relay circuit. |

---

## ⚖️ License

This project is open-source and licensed under the terms of the MIT License.

```

```