# RaveGPT Hardware Reference

## Device Identification

| Role | Board | USB VID:PID | USB Serial Number |
|------|-------|-------------|-------------------|
| Right LED Teensy | LED Display Controller | 16c0:0483 | 19159950 |
| Right FFT Teensy | Audio/FFT + Bridge | 16c0:0483 | 16102920 |
| ESP32 | USB JTAG/Serial | 303a:1001 | DC:54:75:EE:12:A8 |

## LED Strips

- **Type:** WS2812B (single data wire), 144 LED/m, OZES brand
- **Strips:** 6 physical strips
- **LEDs per strip:** 288 (144 up + 144 down zigzag)
- **Total LEDs:** 1728
- **Data pins:** 6, 7, 2, 21, 5, 8

## Pin Assignments

### LED Teensy (teensy_led)
| Pin | Function |
|-----|----------|
| 0 | Serial1 RX (from FFT Teensy) |
| 2 | LED Strip 2 Data |
| 5 | LED Strip 4 Data |
| 6 | LED Strip 0 Data |
| 7 | LED Strip 1 Data |
| 8 | LED Strip 5 Data |
| 21 | LED Strip 3 Data |

### FFT Teensy (teensy_fft)
| Pin | Function |
|-----|----------|
| 0 | Serial1 RX (from ESP32) |
| 1 | Serial1 TX (to LED Teensy) |
| SPDIF | Digital audio input |

## Serial Protocol

- **Baud rate:** 460800 on Serial1
- **Frame format:** `[0xAA][type][seq][len][payload][crc16][0xBB]`
- **FFT payload:** 45 bytes (10 floats + beat float + SPDIF lock byte)
- **CMD payload:** 5 bytes (mode, pattern, color, brightness, flags)

## Power

- **Voltage:** 5V
- **Power per strip:** 43W
- **Power limit:** 258W (configured in FastLED)
