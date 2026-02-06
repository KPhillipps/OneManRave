# OneManRave - Multi-Board LED Controller System

## System Architecture

### Hardware Setup
- **ESP32 Master (Right Side)** - IR sensor control, sends commands to slave
- **ESP32 Slave (Left Side)** - Receives commands from master
- **Teensy 4.0 FFT** - Audio processing, sends FFT data to LED controller
- **Teensy 4.0 LED** - Receives FFT data, controls all LED strips

### Communication Flow
```
IR Sensor → ESP32 Master → ESP32 Slave
                ↓
Audio In → Teensy FFT → Teensy LED → LED Strips
```

## Project Structure

```
oneManRave/
├── esp32_master_right/     # ESP32 with IR sensor (right side)
│   ├── platformio.ini
│   ├── src/main.cpp
│   └── include/Pins.h
│
├── esp32_slave_left/       # ESP32 slave (left side)
│   ├── platformio.ini
│   ├── src/main.cpp
│   └── include/Pins.h
│
├── teensy_fft/            # Teensy 4.0 for FFT processing
│   ├── platformio.ini
│   ├── src/main.cpp
│   └── include/
│
└── teensy_led/            # Teensy 4.0 for LED control
    ├── platformio.ini
    ├── src/
    │   ├── main.cpp
    │   ├── art.cpp
    │   └── Patterns.cpp
    └── include/
```

## Building & Uploading

### To build a specific board:
```bash
# ESP32 Master (Right)
cd esp32_master_right && pio run --target upload

# ESP32 Slave (Left)
cd esp32_slave_left && pio run --target upload

# Teensy FFT
cd teensy_fft && pio run --target upload

# Teensy LED
cd teensy_led && pio run --target upload
```

### Monitor serial output:
```bash
pio device monitor -b 115200
```

## Hardware Connections

### ESP32s Communication
- ESP32 Master TX → ESP32 Slave RX
- Common GND

### Teensy Communication
- Teensy FFT Pin 8 (Serial2 TX) → Teensy LED Pin 7 (Serial2 RX)
- Common GND

### IR Sensor
- Connected to ESP32 Master (Right Side)
