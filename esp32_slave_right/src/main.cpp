// ============================================================
// ESP32 SLAVE (RIGHT CHANNEL) - ESP-NOW Receiver & Serial Forwarder
// ============================================================
// DUAL CHANNEL SYSTEM ARCHITECTURE - RIGHT SIDE:
//   [ESP32 Master] --ESP-NOW--> [THIS ESP32 SLAVE] --Serial--> [FFT Teensy Right] --> [LED Controller Right]
//
// THIS DEVICE (ESP32 Slave - Right Channel):
//   ROLE: Wireless bridge for RIGHT channel control
//   
//   INPUT:
//     - ESP-NOW messages from Master ESP32 (MAC address varies) on WiFi channel 6
//     - Receives struct containing: mode, colorIndex, brightness, param3
//   
//   OUTPUT:
//     - Serial TX Pin 17 @ 38400 baud -> FFT Teensy Right RX Pin 7
//     - Converts ESP-NOW struct to serial command string format
//   
//   OPERATION:
//     1. Listens for ESP-NOW messages from Master ESP32
//     2. When message received, callback function triggers
//     3. Extracts command data from ESP-NOW struct
//     4. Formats as serial command string: "[mode],[val1],[val2],[val3]\n"
//     5. Transmits to RIGHT FFT Teensy via Serial
//   
//   This creates wireless synchronization between LEFT and RIGHT channels
//
// PHYSICAL CONNECTIONS:
//   - ESP-NOW: WiFi channel 6 (wireless from Master, no physical connection)
//   - Serial to RIGHT FFT Teensy: TX Pin 17 (output), RX Pin 18 (unused) @ 38400 baud
//
// IMPORTANT: This ESP32's MAC address (DC:54:75:EE:12:A8) must be configured
//            in the Master ESP32's slaveMacAddress array
// ============================================================

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#include "../include/Pins.h"

// Serial to Teensy
HardwareSerial TeensySerial(1);  // Use UART1

// ESP-NOW data structure (must match master)
typedef struct struct_message {
  char mode;
  uint8_t colorIndex;
  uint8_t brightness;
  uint8_t param3;
} struct_message;

struct_message incomingData;

void onDataRecv(const uint8_t * mac, const uint8_t *incomingData_ptr, int len) {
  // Decode payload: support both struct (4 bytes) and CSV text like "S,0,1,0"
  bool parsed = false;
  char modeChar = 0;
  int color = 0, brightness = 0, param = 0;

  if (len == sizeof(incomingData)) {
    memcpy(&incomingData, incomingData_ptr, sizeof(incomingData));
    modeChar = incomingData.mode;
    color = incomingData.colorIndex;
    brightness = incomingData.brightness;
    param = incomingData.param3;
    parsed = true;
  } else {
    char msg[64];
    int copyLen = (len < (int)sizeof(msg) - 1) ? len : (int)sizeof(msg) - 1;
    memcpy(msg, incomingData_ptr, copyLen);
    msg[copyLen] = '\0';
    if (sscanf(msg, "%c,%d,%d,%d", &modeChar, &color, &brightness, &param) == 4) {
      incomingData.mode = modeChar;
      incomingData.colorIndex = (uint8_t)color;
      incomingData.brightness = (uint8_t)brightness;
      incomingData.param3 = (uint8_t)param;
      parsed = true;
    }
  }

  Serial.println("\n>>> ESP-NOW MESSAGE RECEIVED <<<");
  char macStr[18];
  snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  Serial.print("From Master MAC: ");
  Serial.println(macStr);
  Serial.printf("Len=%d\n", len);

  if (!parsed) {
    Serial.println("WARNING: Could not parse payload");
    return;
  }

  Serial.printf("Mode: %c, Pattern: %d, Brightness: %d\n",
                incomingData.mode, incomingData.colorIndex, incomingData.brightness);

  // Match master format: "mode,val1,val2,val3\n" (no C prefix)
  char buffer[32];
  snprintf(buffer, sizeof(buffer), "%c,%u,%u,%u", incomingData.mode, incomingData.colorIndex,
           incomingData.brightness, incomingData.param3);
  TeensySerial.println(buffer);
  Serial.print("Forwarded to RIGHT FFT Teensy: ");
  Serial.println(buffer);
}

void setup() {
  Serial.begin(DEBUG_SERIAL_BAUD);
  delay(2000);  // Longer delay for USB-CDC serial to initialize on ESP32-S3

  Serial.println("\n\n" + String('=', 60));
  Serial.println("   ESP32 SLAVE (RIGHT CHANNEL)");
  Serial.println("   ESP-NOW Receiver + Serial Forwarder");
  Serial.println(String('=', 60));
  Serial.print("Chip Model: ");
  Serial.println(ESP.getChipModel());
  Serial.print("MAC Address: ");
  Serial.println(WiFi.macAddress());
  Serial.print("Compiled: ");
  Serial.print(__DATE__);
  Serial.print(" ");
  Serial.println(__TIME__);
  Serial.println(String('=', 60) + "\n");


  // Init WiFi in station mode for ESP-NOW
  Serial.println("Initializing WiFi in STA mode...");
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  
  // Set WiFi channel to match master (channel 6)
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(6, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);
  Serial.println("WiFi channel set to 6");

  // Print MAC address
  Serial.print("My MAC Address: ");
  Serial.println(WiFi.macAddress());

  Serial.println("Initializing ESP-NOW...");
  if (esp_now_init() != ESP_OK) {
    Serial.println("ERROR: ESP-NOW initialization failed!");
    return;
  }
  Serial.println("ESP-NOW initialized successfully");

  esp_now_register_recv_cb(onDataRecv);
  Serial.println("Receive callback registered");
  
  // Initialize serial to RIGHT FFT Teensy
  TeensySerial.begin(TEENSY_SERIAL_BAUD, SERIAL_8N1, TEENSY_RX_PIN, TEENSY_TX_PIN);
  Serial.printf("Serial to RIGHT FFT Teensy initialized: TX Pin %d, RX Pin %d @ %d baud\n",
                TEENSY_TX_PIN, TEENSY_RX_PIN, TEENSY_SERIAL_BAUD);

  Serial.println("\n*** ESP-NOW RECEIVER READY - Forwarding to RIGHT FFT Teensy ***\n");
}

void loop() {
  // Nothing here - ESP-NOW callback handles all incoming messages
  delay(1000);
}