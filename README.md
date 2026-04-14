# XIAO ESP32-C3 To-Do List (e-Paper)

A high-performance, low-power e-Paper To-Do list based on the **Seeed Studio XIAO ESP32-C3**. It features a 4.26" e-Paper display with a custom 800x480 layout and a modernized, mobile-friendly Web UI for easy task management.

## 🚀 Key Features

### 📋 Display Layout (800x480)
- **Highlighted Tasks:** The first 3 tasks are prioritized with a larger font size (Size 3) for quick readability.
- **Completed Tasks:** Visible at the bottom of the list, marked with an **"X"** prefix and standard foreground color for maximum visibility on e-Paper.
- **Deadline Column:** Dedicated column for deadlines in `DD-MMM HH:MM` (e.g., `12-Apr 14:30`) or `DD-MMM` format.
- **Top Status Bar:**
    - **Battery Status:** Percentage remaining.
    - **WiFi Status:** Signal strength (Excellent/Good/etc.) and local IP address.
    - **Last Update:** Right-aligned timestamp including date (e.g., `Updated: 12-Apr 14:30`).
- **Dynamic QR Code:** Vertically centered QR code for immediate access to the Web Management Portal.
- **Horizontal Separators:** Clean separation between every task item.

### 📱 Modernized Web Management Portal
- **Task Management:** Add, Delete, Reorder (↑/↓), Toggle Completion, and **Edit/Update** existing tasks.
- **Progress Tracking:** Visual progress bar showing completion percentage (e.g., `5/10 tasks completed`).
- **Exporting:** Download tasks as an **ICS (Calendar)** file or a **TXT** file.
- **Configuration:** Update WiFi credentials and deep sleep duration settings.
- **Touch-Friendly:** Optimized for mobile browsing.

### 🔋 Power & Performance
- **Low Power:** Supports deep sleep and light sleep modes to maximize battery life.
- **Manual Control:** A short press of the power button **instantly toggles** between sleep and wake modes, regardless of the automatic sleep settings.
- **Persistent Storage:** Tasks and settings are saved to the ESP32's flash memory (Preferences API), surviving power loss or restarts.
- **Maintenance Mode:** Hold the power button during boot to enter maintenance mode (automatic sleep disabled).
- **Rotation Control:** Long-press (3s) the power button while awake to flip the display orientation 180 degrees.

## 🛠 Hardware Requirements

- **Microcontroller:** Seeed Studio XIAO ESP32-C3.
- **Display:** 4.26" e-Paper Display (SSD1677) + XIAO ePaper Breakout Board.
- **Power Management:** LiPo Battery (via JST connector) with built-in charging.
- **Buttons:**
    - **Power/Action Button:** IO9 (Boot/User button on XIAO).

### 📍 Pinout (XIAO ESP32-C3)
| Pin | Function | Description |
|---|---|---|
| **D8 (GPIO8)** | SCK | SPI Clock (e-Paper) |
| **D10 (GPIO10)** | MOSI | SPI Data (e-Paper) |
| **D1 (GPIO3)** | CS | Chip Select (e-Paper) |
| **D3 (GPIO5)** | DC | Data/Command (e-Paper) |
| **D0 (GPIO2)** | RST | Reset (e-Paper) |
| **D5 (GPIO7)** | BUSY | Busy Signal (e-Paper) |
| **A4 (GPIO4)** | ADC | Battery Monitoring (2:1 divider) |
| **IO9** | Button | Power/Wake/Maintenance |

## 💻 Software Setup

### Dependencies
- **PlatformIO** (recommended for build & upload).
- **Libraries:**
    - `Seeed_GFX` (Optimized TFT_eSPI fork)
    - `Adafruit GFX Library`
    - `ArduinoJson`
    - `QRCode-esp32`
    - `WiFi`, `WebServer`, `Preferences`, `time.h`, `esp_task_wdt`.

### Installation
1. Clone the repository:
   ```bash
   git clone https://github.com/engkon6/Xiao_ESP32C3_ToDo.git
   cd Xiao_ESP32C3_ToDo
   ```
2. Open in **Visual Studio Code** with the **PlatformIO** extension.
3. Select the `seeed_xiao_esp32c3` environment.
4. Build and Upload: `pio run -e seeed_xiao_esp32c3 -t upload`.

## ⚙️ Configuration
On first boot, if no WiFi credentials are found, the device will enter **Access Point mode**:
- **SSID:** `XiaoTodo-Config`
- **Password:** `12345678`
- **IP:** `192.168.4.1`

Connect your phone or PC to this AP to configure your local WiFi.

## 📄 License
This project is for personal use. Feel free to modify and share!

---
Developed by **engkon6** with **Gemini CLI**.
