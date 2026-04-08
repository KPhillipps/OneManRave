# Teensy 4.0 APA102 FastLED Wiring Test

Minimal wiring test for APA102 strips using FastLED on Teensy 4.0.

## Pin Mapping

- Buffer enable: pin `3` (74HCT245 output enable for RJ45 stage)
- APA102 data pins: `6, 7, 2, 21, 5, 8`
- APA102 shared clock pin: `14`
- LEDs per strip in test: `288`
- Total LED count in test: `1728`

## Pattern

The sketch runs a 2-LED snake across all configured strips.
Only two LEDs are lit at any time for low power draw.

## Build / Upload

From `wiring_test_teensy40/`:

```bash
pio run -e ws2812_fastled_test -t upload
```
