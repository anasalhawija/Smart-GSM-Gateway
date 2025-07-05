# Smart GSM Gateway on ESP8266

<p align="center">
  <img alt="GitHub Stars" src="https://img.shields.io/github/stars/anasalhawija/Smart-GSM-Gateway?style=social">
  <img alt="GitHub Forks" src="https://img.shields.io/github/forks/anasalhawija/Smart-GSM-Gateway?style=social">
  <img alt="GitHub Issues" src="https://img.shields.io/github/issues/anasalhawija/Smart-GSM-Gateway">
  <img alt="License" src="https://img.shields.io/github/license/anasalhawija/Smart-GSM-Gateway">
  <img alt="Last Commit" src="https://img.shields.io/github/last-commit/anasalhawija/Smart-GSM-Gateway">
</p>

> A complete toolkit for bridging the world of GSM communication with modern web technologies. This ESP8266-based gateway is specifically designed to automate interactive USSD services, making it a powerful tool for fintech applications, mobile money management, and IoT automation.

## 🎬 Project Walkthrough: From Setup to Full Operation

This section provides a live, step-by-step demonstration of the gateway's core functionalities.

### **Step 1: First-Time Setup & Configuration**

> The gateway is designed for a seamless out-of-the-box experience. On first boot, it enters Access Point mode, creating its own Wi-Fi network. The user can connect, and a captive portal automatically opens the configuration page to connect the device to a local network.

![Setup Demo GIF](https://raw.githubusercontent.com/anasalhawija/Smart-GSM-Gateway/main/assets/SmartGSMGateway-FirstTimeSetupConfigurat.gif)

---

### **Step 2: Live USSD Interactive Session**

> The core feature of the gateway. The UI provides a dedicated, session-aware form to handle complex, multi-step USSD interactions. This is ideal for automating financial services like balance checks or money transfers from e-wallets.

![USSD Demo GIF](https://raw.githubusercontent.com/anasalhawija/Smart-GSM-Gateway/main/assets/SmartGSMGateway-LiveDemointeractiveUSSD.gif)

---

### **Step 3: Live SMS Demo (Unicode & Real-time)**

> The gateway has a full-featured SMS interface with a robust backend PDU encoder, providing full support for Unicode (Arabic) characters. All interactions are pushed to the UI in real-time via WebSockets.

![SMS Demo GIF](https://raw.githubusercontent.com/anasalhawija/Smart-GSM-Gateway/main/assets/SmartGSMGateway-LiveDemoSMSRealTime.gif)

---

## 🌟 About The Project: A Vision for Accessible Innovation

> As a software engineer with a passion for bridging the gap between low-level protocols and high-level applications, I thrive on tackling the challenges that lie at the intersection of technology and accessibility. This project emerged from a singular vision: **to harness the power of USSD-based financial services, making them more accessible, automated, and seamlessly integrated with contemporary web systems.**

In many developing regions, USSD serves as the backbone of mobile money and e-wallet platforms, such as **Vodafone Cash**. However, interacting with these services programmatically can often be cumbersome and overly complex. To address this, I created the **Smart GSM Gateway**—a comprehensive solution designed to simplify and automate these interactions.

It’s more than just an SMS gateway; it’s a **robust, self-contained platform** that enables developers and businesses to build scalable applications that leverage existing GSM infrastructure.

This project stands as a testament to how thoughtful software design can elevate simple hardware into a powerful tool for **innovation and progress**.

---

## 🎯 Primary Use Cases

- **Fintech & Mobile Money Automation:** Directly interact with and automate services like Vodafone Cash, M-Pesa, etc. Build bots for balance checks, money transfers, and service subscriptions.
- **IoT & M2M Communication:** Use SMS as a reliable, low-bandwidth communication channel for remote devices, sensors, and security alerts.
- **Remote System Management:** Deploy a gateway in a remote location and manage it entirely through its web interface, checking signal quality and network status without physical access.

## ✨ Key Features

- **📱 Interactive USSD at its Core:** A session-aware web form designed specifically to handle complex, multi-step USSD menus, making automation simple and intuitive.
- **💬 Advanced SMS & PDU Engine:** Full support for sending and receiving messages in both **GSM-7 (English)** and **UCS-2 (Arabic)**, thanks to a robust backend PDU encoder/decoder.
- **📡 Real-time Control via WebSockets:** Every action and status update is pushed to the web interface instantly, providing a responsive and modern user experience.
- **📞 Caller ID Notification:** The gateway detects incoming calls and displays the caller's number in the UI.
- **⚙️ Resilient Non-Blocking Backend:** Built with asynchronous state machines in C++ to ensure the gateway remains stable and responsive, even when dealing with unpredictable network latency or unexpected AT command responses.
- **🌐 Dual-Mode & Multilingual:** A self-hosting Access Point for easy setup and a multilingual (AR/EN) Station Mode for daily use.

## 🛠️ Tech Stack

| Category            | Technology / Library                                         |
| :------------------ | :----------------------------------------------------------- |
| **Microcontroller** | `NodeMCU ESP8266`                                            |
| **IDE & Core**      | `PlatformIO IDE` with `ESP8266 Arduino Core v3.0.2`          |
| **Backend (C++)**   | `ESPAsyncWebServer`, `WebSockets`, `ArduinoJson`, `LittleFS` |
| **Frontend (UI)**   | `Vanilla JavaScript (ES6)`, `HTML5`, `CSS3`                  |
| **Communication**   | `AT Commands`, `REST API` (Config), `WebSockets` (Real-time) |

## 🔌 Hardware Setup & Schematic

The hardware setup is minimal and designed for easy replication.

![Wiring Diagram](https://raw.githubusercontent.com/anasalhawija/Smart-GSM-Gateway/main/assets/SmartGSMGateway-PinConnectionSchematic.gif)

1.  **Pin Connections:**

    - `ESP8266 Pin D2 (GPIO4)` → `GSM Module TX`
    - `ESP8266 Pin D1 (GPIO5)` → `GSM Module RX`
    - `ESP8266 GND` → `GSM Module GND` (Ensure a common ground)

2.  **Basic Schematic (ASCII Art):**
    ```
         +---------------------+        +------------------+
         |     NodeMCU         |        |   SIM/GSM Module |
         |       ESP8266       |        |      (SIM900A)   |
         |                     |        |                  |
         |   D2 (GPIO4) Tx  >--|------->| Rx               |
         |   D1 (GPIO5) Rx  <--|-------<| Tx               |
         |                     |        |                  |
         |           GND    <--|------->| GND              |
         +---------------------+        +------------------+
    ```
    **Crucial Power Note:** The SIM900A module is power-hungry. It **must** be powered by its own stable 5V 2A power supply. Do not attempt to power it from the NodeMCU's pins.

## 🚀 Installation & Flashing

To get this project running on your own hardware, follow these steps.

1.  **Clone the repository:**
    ```bash
    git clone https://github.com/anasalhawija/Smart-GSM-Gateway.git
    cd Smart-GSM-Gateway
    ```
2.  **Open in VS Code:** The PlatformIO extension will automatically install all library dependencies.
3.  **Build & Upload:** Open the PlatformIO CLI terminal and execute these commands for a clean installation.

    ```bash
    # 1. Erase flash completely
    pio run -t erase

    # 2. Upload the web interface files
    pio run -t uploadfs

    # 3. Upload the main application firmware
    pio run -t upload
    ```

## 🤝 Contributing

Contributions are what make the open-source community an amazing place to learn, inspire, and create. Any contributions you make are **greatly appreciated**. Please follow **Conventional Commits** for your pull requests.

1.  Fork the Project
2.  Create your Feature Branch (`git checkout -b feature/AmazingFeature`)
3.  Commit your Changes (`git commit -m 'feat: Add some AmazingFeature'`)
4.  Push to the Branch (`git push origin feature/AmazingFeature`)
5.  Open a Pull Request

## 🌟 Acknowledgments

This project stands on the shoulders of giants. My sincere gratitude goes to the developers and maintainers of the incredible open-source libraries that made this gateway possible:

- **Arduino Core for ESP8266:** The fundamental framework for ESP8266 development.
- **PlatformIO IDE:** A powerful and efficient development environment.
- **ESPAsyncWebServer & ESPAsyncTCP:** For robust, asynchronous web server capabilities.
- **WebSockets:** For enabling real-time, bidirectional communication with the web interface.
- **ArduinoJson:** For efficient JSON handling, crucial for data exchange.
- **LittleFS:** For managing the web interface files on the ESP8266's flash memory.

## 📜 License

Distributed under the MIT License. See the [`LICENSE`](LICENSE) file for more information.

---

<p align="center">
  A Project by <b><a href="https://www.linkedin.com/in/anasalhawija">Eng. Anas Alhawija</a></b>
  <br>
  <i>Bridging hardware and software to create innovative solutions.</i>
  <br>
  <a href="https://github.com/anasalhawija">GitHub</a> | <a href="https://www.linkedin.com/in/anasalhawija">LinkedIn</a> | <a href="https://www.tolkt.com">Tolkt.com</a>
</p>
