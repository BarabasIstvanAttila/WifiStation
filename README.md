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
# Create project directory
mkdir esp32-camera-server && cd esp32-camera-server

# Create docker-compose.yml (see Docker section below)

# Start server
docker-compose up -d

# Check it's running
curl http://localhost:8080/health
```

### Step 2: Configure ESP32

Edit `main/config.h`:
```c
#define WIFI_SSID "YourWiFiName"
#define WIFI_PASSWORD "YourPassword"
#define SERVER_URL "http://192.168.1.100:8080/upload"
```

### Step 3: Flash ESP32

```bash
# Install ESP-IDF
git clone --recursive https://github.com/espressif/esp-idf.git
cd esp-idf && ./install.sh && source ./export.sh

# Build and flash
cd /path/to/monthly_camera
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

### Step 4: Test

Uncomment `#define TEST_MODE` in config.h to test every 60 seconds instead of monthly.

---

## 📖 Detailed Setup

### 1. Server Setup (Docker)

#### Create Project Structure

```bash
mkdir -p esp32-camera-server
cd esp32-camera-server
mkdir photos
```

#### Create `app.py`

```python
from flask import Flask, request, jsonify, send_from_directory
import os
from datetime import datetime
import logging

app = Flask(__name__)

UPLOAD_FOLDER = os.getenv('UPLOAD_FOLDER', '/data/photos')
AUTH_USERNAME = os.getenv('AUTH_USERNAME', 'admin')
AUTH_PASSWORD = os.getenv('AUTH_PASSWORD', 'password')
REQUIRE_AUTH = os.getenv('REQUIRE_AUTH', 'true').lower() == 'true'

logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)

os.makedirs(UPLOAD_FOLDER, exist_ok=True)

def check_auth():
    if not REQUIRE_AUTH:
        return True
    auth = request.authorization
    return auth and auth.username == AUTH_USERNAME and auth.password == AUTH_PASSWORD

@app.route('/health')
def health():
    return jsonify({'status': 'ok', 'timestamp': datetime.now().isoformat()})

@app.route('/upload', methods=['POST'])
def upload():
    if not check_auth():
        return jsonify({'error': 'Unauthorized'}), 401
    
    filename = request.headers.get('X-Filename', f'photo_{datetime.now().strftime("%Y%m%d_%H%M%S")}.jpg')
    filepath = os.path.join(UPLOAD_FOLDER, os.path.basename(filename))
    
    with open(filepath, 'wb') as f:
        f.write(request.data)
    
    logger.info(f"Uploaded: {filename} ({len(request.data)} bytes)")
    return jsonify({'status': 'success', 'filename': filename})

@app.route('/list')
def list_files():
    if not check_auth():
        return jsonify({'error': 'Unauthorized'}), 401
    
    files = []
    for f in os.listdir(UPLOAD_FOLDER):
        if os.path.isfile(os.path.join(UPLOAD_FOLDER, f)):
            stat = os.stat(os.path.join(UPLOAD_FOLDER, f))
            files.append({
                'filename': f,
                'size': stat.st_size,
                'modified': datetime.fromtimestamp(stat.st_mtime).isoformat()
            })
    
    return jsonify({'files': sorted(files, key=lambda x: x['modified'], reverse=True)})

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=8080)
```

#### Create `Dockerfile`

```dockerfile
FROM python:3.11-slim

WORKDIR /app

COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

COPY app.py .

RUN mkdir -p /data/photos

EXPOSE 8080

HEALTHCHECK --interval=30s --timeout=3s \
    CMD python -c "import urllib.request; urllib.request.urlopen('http://localhost:8080/health')"

CMD ["python", "app.py"]
```

#### Create `requirements.txt`

```
Flask==3.0.0
Werkzeug==3.0.1
```

#### Create `docker-compose.yml`

```yaml
version: '3.8'

services:
  esp32-server:
    build: .
    container_name: esp32-camera-server
    restart: unless-stopped
    ports:
      - "8080:8080"
    environment:
      - UPLOAD_FOLDER=/data/photos
      - AUTH_USERNAME=admin
      - AUTH_PASSWORD=changeme123
      - REQUIRE_AUTH=true
    volumes:
      - ./photos:/data/photos
```

#### Start Server

```bash
# Build and start
docker-compose up -d

# Check logs
docker-compose logs -f

# Test
curl http://localhost:8080/health
```

#### Mounting Samba Share (Optional)

```bash
# Mount Samba share on host
sudo apt install cifs-utils
sudo mkdir -p /mnt/samba_share
sudo mount -t cifs //192.168.1.10/photos /mnt/samba_share \
  -o username=user,password=pass

# Update docker-compose.yml volumes:
volumes:
  - /mnt/samba_share:/data/photos

# Restart
docker-compose restart
```

---

### 2. ESP32 Setup

#### Install ESP-IDF

```bash
# Clone ESP-IDF
git clone --recursive https://github.com/espressif/esp-idf.git
cd esp-idf

# Install
./install.sh

# Setup environment (run this in each new terminal)
source ./export.sh
```

#### Project Structure

```
monthly_camera/
├── CMakeLists.txt
├── main/
│   ├── CMakeLists.txt
│   ├── config.h
│   └── main.c
├── sdkconfig.defaults
└── partitions.csv
```

#### Create `CMakeLists.txt` (root)

```cmake
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(monthly_camera)
```

#### Create `main/CMakeLists.txt`

```cmake
idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
)
```

#### Create `sdkconfig.defaults`

```ini
CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y
CONFIG_CAMERA_MODEL_AI_THINKER=y
CONFIG_ESP_WIFI_SSID=""
CONFIG_ESP_WIFI_PASSWORD=""
```

#### Create `partitions.csv`

```csv
# Name,   Type, SubType, Offset,  Size
nvs,      data, nvs,     0x9000,  0x6000,
phy_init, data, phy,     0xf000,  0x1000,
factory,  app,  factory, 0x10000, 0x3F0000,
```

#### Configure

```bash
cd monthly_camera

# Edit config.h with your WiFi and server details
nano main/config.h

# Optional: Run menuconfig for advanced settings
idf.py menuconfig
```

---

### 3. Flashing ESP32

#### Method A: ESP32-CAM-MB (Easiest)

```bash
# 1. Insert ESP32-CAM into MB board
# 2. Connect USB cable
# 3. Check device
ls /dev/ttyUSB*  # Linux
ls /dev/cu.*     # macOS
# Check Device Manager on Windows

# 4. Build
idf.py build

# 5. Flash
idf.py -p /dev/ttyUSB0 flash

# 6. Monitor (optional)
idf.py -p /dev/ttyUSB0 monitor
# Press Ctrl+] to exit
```
---

## ⚙️ Configuration

### WiFi Settings

```c
#define WIFI_SSID "YourNetwork"
#define WIFI_PASSWORD "YourPassword"
```

### Server Settings

```c
#define SERVER_URL "http://192.168.1.100:8080/upload"
#define SERVER_USERNAME "admin"
#define SERVER_PASSWORD "password"
```

### Timezone

Find your timezone string at: https://github.com/nayarsystems/posix_tz_db/blob/master/zones.csv

```c
#define TIMEZONE "EET-2EEST,M3.5.0/3,M10.5.0/4"
```

### Test Mode

For testing, uncomment this line in `config.h`:

```c
#define TEST_MODE  // Captures every 60 seconds
```

Don't forget to comment it out for production!

### Schedule

```c
#define CAPTURE_HOUR_START 12  // Start at noon
#define CAPTURE_HOUR_END   14  // Stop at 2 PM
```

---

## 🧪 Testing

### 1. Test Server

```bash
# Health check
curl http://192.168.1.100:8080/health

# Test upload
curl -X POST http://192.168.1.100:8080/upload \
  -H "X-Filename: test.jpg" \
  --data-binary @test.jpg \
  -u admin:password

# List files
curl http://192.168.1.100:8080/list -u admin:password
```

### 2. Test ESP32 in Test Mode

```c
// In config.h:
#define TEST_MODE

// Rebuild and flash:
idf.py build flash monitor

// Should capture every 60 seconds
```

### 3. Monitor Battery

```bash
# In monitor output, look for:
I (xxx) monthly_camera: Battery: 4.15V
```

---

## 🔧 Troubleshooting

### ESP32 Won't Flash

**Problem:** "Failed to connect"

**Solution:**
```bash
# Lower baud rate
idf.py -p /dev/ttyUSB0 -b 115200 flash

# Check wiring (FTDI):
# - Swap TX/RX if needed
# - Ensure IO0 is connected to GND during flash

# Check USB cable (try different cable)

# Check permissions (Linux):
sudo usermod -a -G dialout $USER
# Logout and login
```

### Camera Init Fails

**Problem:** "Camera init failed: 0x105"

**Solution:**
```bash
# Reseat camera ribbon cable
# Check 5V power supply (needs >500mA)
# Try powered USB hub
# Check if camera module is damaged
```

### WiFi Connection Fails

**Problem:** "WiFi connection failed"

**Solution:**
```c
// Check SSID and password
// Try increasing timeout:
#define WIFI_TIMEOUT_MS 60000

// Check WiFi signal strength
// Try 2.4GHz network (ESP32 doesn't support 5GHz)
```

### Upload Fails

**Problem:** "Upload failed"

**Solution:**
```bash
# Check server is running:
curl http://SERVER_IP:8080/health

# Check firewall:
sudo ufw allow 8080

# Check ESP32 can reach server:
ping SERVER_IP  # from your network

# Verify server IP in config.h
```

### Time Sync Fails

**Problem:** "Time sync failed"

**Solution:**
```bash
# Check internet connection
# Try different NTP server in code
# Check firewall allows UDP port 123
# Increase retry count
```

### Battery Drains Fast

**Problem:** Device dies in days instead of months

**Solution:**
```bash
# Measure deep sleep current with multimeter
# Should be 10-150 µA

# Check:
# - Flash LED is off (should be)
# - WiFi is disconnected (should be)
# - No USB connected during operation
# - Battery capacity is adequate (3000mAh recommended)

# Disable test mode:
// #define TEST_MODE  // Comment this out!
```

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
esp32-camera-server/          # Server
├── app.py
├── Dockerfile
├── docker-compose.yml
├── requirements.txt
└── photos/                   # Uploaded photos

monthly_camera/               # ESP32
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

## 📝 License

Public Domain / CC0

---

## 🆘 Support

1. Check troubleshooting section
2. Enable test mode and monitor serial output
3. Test server separately
4. Check all connections and settings

## 🎉 Success!

If you see photos appearing in your `photos/` folder on the last day of each month, congratulations! Your ESP32 camera is working perfectly.