# IoT Smart Greenhouse Automation System

Distributed IoT solution for monitoring and automating a smart greenhouse. The system relies on multiple ESP32 microcontrollers communicating via MQTT, with a centralized Node-RED server for data orchestration, storage (MongoDB), and user interfaces (Web Dashboard and Telegram Bot).

> **Note:** The source code and this README are in English, but the detailed technical documentation (`Memoria_Inf_Ind.pdf`)[cite: 1] and user manual located in the `docs/` folder are written in Spanish.

## Architecture & Nodes

The system is divided into three independent hardware nodes to modularize tasks:

1. **Sensor Node (Data Acquisition)**[cite: 4, 5]
   * **Hardware:** ESP32, DHT11 (Temperature & Humidity), LDR (Light), Capacitive Soil Moisture Sensor, 16x2 I2C LCD[cite: 4].
   * **Software:** Built on FreeRTOS[cite: 7]. Uses hardware interrupts and a binary semaphore to decouple data reading from MQTT publishing[cite: 7]. Implements Deep Sleep (5-minute cycles) to optimize power consumption[cite: 7, 12].
2. **Actuator Node (Environment Control)**[cite: 4, 5]
   * **Hardware:** ESP32, Submersible Water Pump (Relay), 180º Servomotor (Ventilation window), DC Fan[cite: 5].
   * **Software:** Reactive logic subscribed to MQTT control topics. Actuates upon receiving specific payloads to trigger watering (30s) or ventilation (3 mins)[cite: 7, 8].
3. **Camera Node (Surveillance)**[cite: 5]
   * **Hardware:** ESP32-CAM (OV2640)[cite: 5].
   * **Software:** Continuous operation via FreeRTOS. Uses a Producer-Consumer model[cite: 8]. Captures images triggered by either a remote MQTT command or a 1-hour hardware timer[cite: 8]. Sends Base64 encoded images via HTTPS POST to the backend[cite: 8, 9]. Brownout detector disabled by software to prevent instability during high current spikes (WiFi + Camera)[cite: 6, 12].

## Backend & Interfaces

* **Node-RED Server:** Handles business logic, automated triggers (e.g., watering when soil moisture drops below 55%)[cite: 9], and integrates the AEMET API for weather forecasting[cite: 9].
* **MongoDB:** Persistent storage for sensor telemetry and captured images[cite: 9].
* **Web Dashboard:** Real-time gauges, historical line charts, and time-lapse viewer for the greenhouse camera[cite: 10].
* **Telegram Bot:** Interactive conversational interface (`@Invernadero_Grupo11_Bot`) for remote querying and manual actuation[cite: 11]. Implements safety checks (double-confirmation) if a user tries to trigger an action that contradicts sensor data[cite: 11, 12].

## Tech Stack & Libraries

* **Framework:** Arduino core for ESP32.
* **OS:** FreeRTOS (Task management, Semaphores, Software Timers)[cite: 7, 8].
* **Connectivity:** `WiFi.h`, `PubSubClient.h` (MQTT), `HTTPClient.h`[cite: 13].
* **Sensors/Actuators:** `DHTesp.h`, `ESP32Servo.h`, `LiquidCrystal_I2C.h`, `esp_camera.h`[cite: 13].

## Repository Structure

```text
├── src/
│   ├── sensor_node/        # FreeRTOS-based telemetry acquisition
│   ├── actuator_node/      # MQTT reactive actuators
│   └── camera_node/        # Image capture and HTTPS POST transmission
├── node-red/
│   └── flows.json          # Complete Node-RED flow (Dashboard, Logic, Telegram)
└── docs/
    ├── Memoria_Inf_Ind.pdf # Technical specification (ES)```
    └── Hardware Schematics
```
## Video Demonstration

Watch the automated greenhouse in action:

[![Smart Greenhouse Demo](https://img.youtube.com/vi/sc27La4RPKg/0.jpg)]([https://www.youtube.com/watch?v=YOUR_VIDEO_ID_HERE](https://youtu.be/sc27La4RPKg?feature=shared))


## Authors

* **Arturo Ríos Pérez**
* **Adrián Sola Nieto**
* **Rafael Martín Benítez**
