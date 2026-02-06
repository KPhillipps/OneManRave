ESP SLAVE RIGHT - Receiver Only
================================

⚠️ **NO LEDs ON THIS DEVICE** ⚠️

This project is the RIGHT slave ESP32 (Adafruit ESP32-S3 Feather) that receives ESP-NOW commands from the master ESP and logs them to serial.

**This device does NOT control LEDs - it only receives and logs messages.**

Quick start (macOS)

1. Connect the Feather via USB-C.
2. Note the device path (example): `/dev/cu.usbmodem1301`.
   - Use `ls -la /dev/tty.* | grep -i usb` to list ports.

Build, upload, monitor

- Build:

```bash
platformio run -e esp32s3feather
```

- Upload:

```bash
platformio run -e esp32s3feather -t upload
```

- Serial monitor (PlatformIO built-in):
  - Use the PlatformIO VS Code quick access: Project Tasks -> esp32s3feather -> Monitor
  - Or from terminal (replace port):

```bash
pio device monitor -p /dev/cu.usbmodem1301 -b 115200
```

Config

- Edit `include/Pins.h` to match your LED wiring (`LED_DATA_PIN`, `LED_CLOCK_PIN`, `LED_NUM`).
- `platformio.ini` already includes `FastLED` as a dependency for the slave project.

Notes

- This is a minimal receiver example. It prints incoming ESP-NOW payloads to serial and performs simple FastLED actions for commands starting with `S`, `M`, `P`, or `A`.
- Adjust parsing and behavior in `src/main.cpp` to match your master's payload format and LED wiring.
