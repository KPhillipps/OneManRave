#ifndef ESP_PINS_H
#define ESP_PINS_H

// ============================================================
// ESP32 SLAVE (RIGHT CHANNEL) - Pin Definitions
// ============================================================

// Serial debug (USB CDC)
#define DEBUG_SERIAL_BAUD 115200

// Serial to RIGHT FFT Teensy (uses board's labeled TX/RX pins)
// On Adafruit ESP32-S3 Feather, TX and RX are the hardware UART pins
#define TEENSY_TX_PIN TX   // ESP TX pin -> Teensy Serial1 RX Pin 0 @ 38400 baud
#define TEENSY_RX_PIN RX   // ESP RX pin <- Teensy TX (not used, one-way communication)
#define TEENSY_SERIAL_BAUD 38400

#endif
