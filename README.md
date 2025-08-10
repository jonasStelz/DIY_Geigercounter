# ESP32-S3 Geiger Counter PCB

A custom-designed **battery-powered** Geiger counter board created in **KiCad**, featuring an **ESP32-S3-WROOM-1** module, an LCD display, and a high-voltage section for driving a Geiger–Müller tube.

This project combines radiation detection with IoT capabilities, allowing live data display and wireless transmission without being tethered to a power source.

---

## Features

- **ESP32-S3-WROOM-1** microcontroller for high performance and Wi-Fi/Bluetooth connectivity  
- **LCD display** for real-time radiation readings and system status  
- Integrated **high-voltage generator** for Geiger–Müller tube operation  
- **USB-C** connector for charging and programming  
- **Battery-powered** for portable use (Li-ion/LiPo support)  
- Compact PCB layout for handheld or embedded use  
- Designed entirely in **KiCad**

---

## Hardware Overview

The PCB integrates both the high-voltage section (for the tube) and the low-voltage digital section (ESP32 + display + battery management) on the same board.  
Special care has been taken to separate HV signals from sensitive logic lines.

---

## Feedback & Contributions

This is my first PCB design combining high-voltage circuitry with an ESP32-S3 microcontroller.  
I welcome **design reviews, suggestions, and improvements** — especially regarding:  
- HV layout and safety clearances  
- EMC and noise reduction  
- Power supply stability (especially with battery use)  
- General PCB best practices  

If you spot an issue or have an idea, please open an **Issue** or submit a **Pull Request**.

---
