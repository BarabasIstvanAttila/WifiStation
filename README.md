# WifiStation

Ultra-low power camera system that captures photos on the last day of each month and uploads them to a network server. Perfect for time-lapse photography, monitoring, or documentation.

## 📋 Table of Contents

- [Features](#features)
- [Hardware Requirements](#hardware-requirements)
- [Quick Start](#quick-start)
- [Detailed Setup](#detailed-setup)
  - [1. Server Setup (Docker)](#1-server-setup-docker)
  - [2. ESP32 Setup](#2-esp32-setup)
  - [3. Flashing ESP32](#3-flashing-esp32)
- [Configuration](#configuration)
- [Testing](#testing)
- [Troubleshooting](#troubleshooting)
- [Power Consumption](#power-consumption)

## ✨ Features

- ✅ Captures photo on last day of each month
- ✅ Automatic WiFi connection and upload
- ✅ Battery powered with ultra-low power consumption
- ✅ Flash LED for low-light conditions
- ✅ Test mode for development
- ✅ Battery voltage monitoring
- ✅ Docker-based upload server
- ✅ Basic authentication
- ✅ Automatic time synchronization

## 🛠 Hardware Requirements

### ESP32-CAM Setup
- **ESP32-CAM** module (AI-Thinker recommended)
- **ESP32-CAM-MB** programmer board OR **FTDI USB-to-Serial** adapter
- **USB Cable** (Micro-USB or USB-C depending on programmer)
- **Battery**: 3.7V Li-ion (3000mAh recommended) OR 3x AA with 5V regulator
- **Optional**: 2x 10kΩ resistors for battery voltage monitoring

### Server
- Computer/Raspberry Pi/NAS with Docker
- Network storage (local folder or Samba share)

## 🚀 Quick Start

### Step 1: Deploy Upload Server

```bash
# Start server
docker-compose up -d

# Check it's running
curl http://localhost:8080/health
```

### Step 2: Configure ESP32

```bash
# Install ESP-IDF
git clone --recursive https://github.com/espressif/esp-idf.git
cd esp-idf && ./install.sh && source ./export.sh

# 1. Configure — set WiFi, endpoint URL, credentials
idf.py menuconfig
# → Camera Web Server Configuration → WiFi
# → Camera Web Server Configuration → Cloud Upload
# → Camera Web Server Configuration → NTP Time Synchronisation

# -- Server url: "http://192.168.1.100:8080/upload"

# Add extra components
# Fetch the managed camera package
idf.py add-dependency "espressif/esp32-camera"
```

### Step 3: Flash ESP32

```bash
# Read the esp 32 doc for setup
# https://docs.espressif.com/projects/esp-idf/en/v4.3/esp32/get-started/index.html
# Alias enabled: alias get_idf='. $HOME/esp/esp-idf/export.sh' 
# Loads the right path to profile: source ~/.profile
# Loads the dev env: get_idf
```bash

```bash
# Grant permissions: 
sudo usermod -a -G dialout $USER
# Apply chamges
newgrp dialout
# Check port
ls /dev/tty*
```

```bash
# Build and flash
cd /path/to/monthly_camera
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

### Step 4: Test
Enable test mode for bench validation (uploads every 60 s)
menuconfig → Camera Web Server Configuration → Test Mode → Enable test upload mode

---


## 🔋 Power Consumption

### Current Draw

- **Deep Sleep:** 10-150 µA
- **Active (WiFi + Camera):** 200-300 mA for 30-60 seconds
- **Monthly Active Time:** ~1 minute per month

### Battery Life Calculator

```
Battery Life = Battery Capacity / Average Current

For 3000mAh battery:
- Deep Sleep: ~30 days × 24h × (100µA) = 72mAh/month
- Active: 1 minute × (250mA) = 4.2mAh/month
- Total: ~76mAh/month

Expected Life = 3000mAh / 76mAh = ~39 months (3+ years)
```

### Extending Battery Life

1. Use larger battery (10,000mAh = 10+ years)
2. Reduce photo quality (smaller uploads)
3. Use solar panel + charge controller
4. Reduce capture frequency (quarterly instead of monthly)

---

## 📊 Monitoring

### View Logs

```bash
# ESP32 (connected via USB)
idf.py monitor

# Docker server
docker-compose logs -f
```

### Check Photos

```bash
# List uploaded photos
ls -lh photos/

# View in browser
curl http://localhost:8080/list -u admin:password
```

### Battery Monitoring

Add voltage divider circuit:
```
Battery+ ----[10kΩ]---- GPIO36 (ADC) ----[10kΩ]---- GND
```

Voltage reading will show in serial monitor.

---

## 📦 Project Files Summary

```
Backend/          # Server
├── app.py
├── Dockerfile
├── docker-compose.yml
├── requirements.txt
└── photos/                   # Uploaded photos

esp_app/               # ESP32
├── CMakeLists.txt
├── main/
│   ├── CMakeLists.txt
│   ├── config.h
│   └── main.c
├── sdkconfig.defaults
└── partitions.csv
```

---

## 🎯 Production Checklist

- [ ] Test mode disabled
- [ ] WiFi credentials correct
- [ ] Server URL correct
- [ ] Timezone configured
- [ ] Battery voltage monitoring working
- [ ] Server authentication enabled
- [ ] Photos folder backed up
- [ ] Tested full capture-upload cycle
- [ ] Measured battery consumption
- [ ] Server has enough storage

---

## 🆘 Support

1. Check troubleshooting section
2. Enable test mode and monitor serial output
3. Test server separately
4. Check all connections and settings

## 🎉 Success!

If you see photos appearing in your `photos/` folder on the last day of each month, congratulations! Your ESP32 camera is working perfectly.