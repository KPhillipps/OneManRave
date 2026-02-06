#ifndef ESP_PINS_H
#define ESP_PINS_H

// Adafruit ESP32-S3 Feather pins

// Serial to Teensy (HardwareSerial used in main)
#define TEENSY_TX_PIN 17   // ESP TX -> Teensy Serial1 RX Pin 0
#define TEENSY_RX_PIN 18   // ESP RX <- Teensy (not used)
#define TEENSY_SERIAL_BAUD 38400

// IR Receiver pin for Apple remote
#define IR_RECV_PIN 13   // IR sensor data pin

#endif
