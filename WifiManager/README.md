# ESP32 Modern Async WiFi Manager

This project implements a robust and modern WiFi Manager for the ESP32 using asynchronous web server libraries. It allows users to easily configure the device's WiFi connection through a sleek, responsive web interface without hardcoding credentials.

## 🌟 Key Features

### 1. **Seamless WiFi Configuration (AP + STA Mode)**
*   **Dual Mode**: The ESP32 operates in Access Point (AP) mode for configuration while simultaneously acting as a Station (STA) to connect to your home network.
*   **Auto-Scan**: Automatically scans for available WiFi networks upon opening the setup page.
*   **Non-Blocking**: Utilizes asynchronous scanning and connection handling to ensure the web server remains responsive and prevents Watchdog Timer (WDT) crashes.

### 2. **Modern Web Interface**
*   **Aesthetics**: Features a premium dark theme with **Glassmorphism** effects (blur, transparency), smooth animations, and a polished color palette.
*   **User Experience**:
    *   Real-time feedback for scanning and connection status.
    *   Interactive list of available networks with signal strength (RSSI) indicators.
    *   Visual confirmation upon successful connection with the new IP address.
*   **Single Page Application (SPA) Feel**: Uses JavaScript (Fetch API) to handle data without full page reloads.

### 3. **Device Control Dashboard**
*   Once connected, the device serves a dedicated control page (`index.html`).
*   **Remote Control**: Toggle the built-in LED (or any connected relay/actuator) directly from the web interface.
*   **State Synchronization**: The UI updates to reflect the actual state of the device.

### 4. **Technical Reliability**
*   **Async Architecture**: Built on `AsyncTCP` and `ESPAsyncWebServer` (specifically the `esp32async` forks) for high performance and stability.
*   **LittleFS Storage**: Web assets (HTML, CSS) are stored in the ESP32's flash memory using LittleFS, keeping the C++ code clean and the frontend easily modifiable.
*   **Watchdog Safety**: Implements non-blocking polling mechanisms for WiFi tasks to avoid system resets during long operations.

## 🛠️ Tech Stack

*   **Hardware**: ESP32 Development Board
*   **Framework**: Arduino (PlatformIO)
*   **Libraries**:
    *   `esp32async/ESPAsyncWebServer`
    *   `esp32async/AsyncTCP`
    *   `bblanchon/ArduinoJson`
*   **Frontend**: HTML5, CSS3 (Variables, Flexbox, Animations), Vanilla JavaScript.

## 🚀 How to Use

1.  **Setup**: Power on the ESP32. It will create a WiFi network named **`ESP32-Setup`**.
2.  **Connect**: Connect your phone or laptop to this network.
3.  **Configure**:
    *   Open a browser and navigate to `http://192.168.4.1`.
    *   Wait for the scan to complete.
    *   Select your WiFi network and enter the password.
    *   Click **Connect**.
4.  **Control**:
    *   Once connected, the page will display the new IP address.
    *   Switch your device back to your main WiFi network.
    *   Navigate to the new IP address to access the **Device Control** dashboard.

## 📂 Project Structure

*   **`src/main.cpp`**: Core firmware logic handling WiFi modes, async web routes, and hardware control.
*   **`data/`**:
    *   `wifi.html`: The setup interface.
    *   `index.html`: The control dashboard.
    *   `style.css`: Shared styling for a consistent look.
*   **`platformio.ini`**: Project configuration and dependency management.
