# RaveGPT Hardware Reference

## Device Identification

| Role | Board | USB VID:PID | USB Serial Number |
|------|-------|-------------|-------------------|
| Right LED Teensy | LED Display Controller | 16c0:0483 | 19159950 |
| Right FFT Teensy | Audio/FFT + Bridge | 16c0:0483 | 16102920 |
| ESP32 | USB JTAG/Serial | 303a:1001 | DC:54:75:EE:12:A8 |

## LED Strips

- **Type:** APA102 (4-wire SPI)
- **Strips:** 6 physical strips
- **LEDs per strip:** 288 (144 up + 144 down zigzag)
- **Total LEDs:** 1728
- **Clock pin:** 13 (shared)
- **Data pins:** 2, 3, 4, 5, 16, 18

## Pin Assignments

### LED Teensy (teensy40_led)
| Pin | Function |
|-----|----------|
| 0 | Serial1 RX (from FFT Teensy) |
| 2 | LED Strip 0 Data |
| 3 | LED Strip 1 Data |
| 4 | LED Strip 2 Data |
| 5 | LED Strip 3 Data |
| 13 | APA102 Clock (all strips) |
| 16 | LED Strip 4 Data |
| 18 | LED Strip 5 Data |

### FFT Teensy (teensy40_fft)
| Pin | Function |
|-----|----------|
| 8 | Serial2 TX (to LED Teensy) |
| SPDIF | Digital audio input |

## Serial Protocol

- **Baud rate:** 460800
- **Frame format:** `[0xAA][type][seq][len][payload][crc16][0xBB]`
- **FFT payload:** 45 bytes (10 floats + beat float + SPDIF lock byte)
- **CMD payload:** 5 bytes (mode, pattern, color, brightness, flags)

## Power

- **Voltage:** 5V
- **Max current:** 50A
- **Power limit:** 250W (configured in FastLED)
