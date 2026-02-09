// ============================================================================
// ESP32 MASTER (LEFT CHANNEL) - IR Remote & Dual-Channel Controller
// ============================================================================
// DUAL CHANNEL SYSTEM ARCHITECTURE:
//
//   LEFT CHANNEL:                          RIGHT CHANNEL:
//   [IR Remote]                            (mirrors left)
//        |
//   [THIS ESP32 MASTER] --Serial--> [FFT Teensy Left] --> [LED Controller Left]
//        |
//        +--------ESP-NOW--------> [ESP32 SLAVE Right] --Serial--> [FFT Teensy Right] --> [LED Controller Right]
//
// THIS DEVICE (ESP32 Master - Left Channel):
//   ROLE: Single point of control for BOTH channels
//   
//   INPUT:
//     - IR commands from Apple Remote (Pin 13)
//   
//   OUTPUTS:
//     - LEFT CHANNEL:  Serial TX Pin 17 @ 38400 baud -> FFT Teensy Left RX Pin 7
//     - RIGHT CHANNEL: ESP-NOW over WiFi -> ESP32 Slave (MAC DC:54:75:EE:12:A8)
//   
//   OPERATION:
//     1. Receives IR commands from Apple Remote
//     2. Sends identical commands to BOTH channels simultaneously:
//        - LEFT:  Direct Serial transmission to FFT Teensy Left
//        - RIGHT: ESP-NOW wireless transmission to ESP32 Slave
//                 (Slave then forwards via Serial to FFT Teensy Right)
//
// PHYSICAL CONNECTIONS:
//   - IR Receiver: Pin 13 (IR sensor data input)
//   - Serial to LEFT FFT Teensy: TX Pin 17 (output), RX Pin 18 (unused) @ 38400 baud
//   - ESP-NOW: WiFi channel 6 to ESP32 Slave MAC DC:54:75:EE:12:A8
//
// Both channels receive identical commands for synchronized stereo operation
// ============================================================================

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <IRremoteESP8266.h>
#include <IRrecv.h>
#include <IRutils.h>
#include "../include/Pins.h"

// IR Receiver
IRrecv irrecv(IR_RECV_PIN);
decode_results results;

// ESP-NOW slave MAC address
uint8_t slaveMacAddress[] = {0xDC, 0x54, 0x75, 0xEE, 0x12, 0xA8}; // Slave's MAC

// ESP-NOW data structure
typedef struct struct_message {
  char mode;
  uint8_t colorIndex;
  uint8_t brightness;
  uint8_t param3;
} struct_message;

struct_message outgoingData;

// Serial communication to Teensy
HardwareSerial TeensySerial(1); // Use UART1

// State variables
char mode = 'S';        // S=Solid, M=Music, A=Animation, P=Pattern
uint8_t colorIndex = 1;
uint8_t brightness = 3;
uint8_t param3 = 0;

// Apple Remote button codes (NEC protocol)
#define APPLE_PLAY      0x77E15020  // Center/Play button
#define APPLE_MENU      0x77E1C020  // Menu button
#define APPLE_RIGHT     0x77E1E020  // Right button
#define APPLE_LEFT      0x77E1D020  // Left button
#define APPLE_UP        0x77E1A020  // Up (volume up)
#define APPLE_DOWN      0x77E1B020  // Down (volume down)
#define APPLE_REPEAT    0xFFFFFFFF  // Repeat code

unsigned long lastCommandTime = 0;
const unsigned long REPEAT_DELAY = 150; // ms between repeat commands

void sendCommand(char cmd, uint8_t val1, uint8_t val2, uint8_t val3) {
  // Send format: "mode,val1,val2,val3\n" for LED Teensy
  // This function sends to BOTH channels:
  //   1. LEFT channel via Serial (direct to Teensy FFT Left)
  //   2. RIGHT channel via ESP-NOW (to Slave ESP32, which forwards to Teensy FFT Right)
  char buffer[32];
  int len = snprintf(buffer, sizeof(buffer), "%c,%u,%u,%u", cmd, val1, val2, val3);
  if (len < 0 || len >= (int)sizeof(buffer)) {
    Serial.println("Command format error - skipped send");
    return;
  }
  
  Serial.println("\n>>> SENDING TO TEENSY <<<");
  Serial.print("Buffer: ");
  Serial.println(buffer);
  Serial.print("Length: ");
  Serial.println(strlen(buffer));
  
  size_t bytesWritten = TeensySerial.write((uint8_t *)buffer, len);
  bytesWritten += TeensySerial.write('\n'); // explicit newline to match Teensy parser
  Serial.print("Bytes written to Serial (incl. newline): ");
  Serial.println(bytesWritten);
  Serial.print("TX Pin 17 to Teensy RX Pin 7\n");
  
  // Force flush to ensure transmission
  TeensySerial.flush();
  Serial.println("Serial flushed - LEFT channel transmission complete");
  
  // ---------------------------------------------------------------
  // RIGHT CHANNEL: Send same command via ESP-NOW to Slave ESP32
  // Slave will receive this and forward to Teensy FFT Right
  // ---------------------------------------------------------------
  outgoingData.mode = cmd;
  outgoingData.colorIndex = val1;
  outgoingData.brightness = val2;
  outgoingData.param3 = val3;
  
  esp_err_t result = esp_now_send(slaveMacAddress, (uint8_t *) &outgoingData, sizeof(outgoingData));
  if (result == ESP_OK) {
    Serial.println("ESP-NOW sent successfully to Slave (RIGHT channel)");
  } else {
    Serial.println("ESP-NOW send failed to Slave");
  }
}

void handleIRCommand(uint32_t command) {
  unsigned long now = millis();
  
  // Ignore repeat codes that come too fast
  if (command == APPLE_REPEAT && (now - lastCommandTime) < REPEAT_DELAY) {
    return;
  }
  
  lastCommandTime = now;
  
  switch (command) {
    case APPLE_PLAY:
      // Toggle modes: S -> M -> S
      if (mode == 'S') {
        mode = 'M';
      } else {
        mode = 'S';
      }
      sendCommand(mode, colorIndex, brightness, param3);
      break;
      
    case APPLE_MENU:
      // Cycle modes: M -> A -> P -> M
      if (mode == 'M') mode = 'A';
      else if (mode == 'A') mode = 'P';
      else if (mode == 'P') mode = 'M';
      else mode = 'M'; // Default to M if in S
      sendCommand(mode, colorIndex, brightness, param3);
      break;
      
    case APPLE_RIGHT:
    case APPLE_REPEAT:
      {
        // Increment pattern/color, wrap per mode
        uint8_t maxIdx = (mode == 'M') ? 12 : (mode == 'P') ? 6 : 11;
        colorIndex++;
        if (colorIndex > maxIdx) colorIndex = 0;
        sendCommand(mode, colorIndex, brightness, param3);
      }
      break;

    case APPLE_LEFT:
      {
        // Decrement pattern/color, wrap per mode
        uint8_t maxIdx = (mode == 'M') ? 12 : (mode == 'P') ? 6 : 11;
        if (colorIndex == 0) colorIndex = maxIdx;
        else colorIndex--;
        sendCommand(mode, colorIndex, brightness, param3);
      }
      break;
      
    case APPLE_UP:
      // Increase brightness (0-27)
      if (brightness < 27) brightness++;
      sendCommand(mode, colorIndex, brightness, param3);
      break;
      
    case APPLE_DOWN:
      // Decrease brightness (0-27)
      if (brightness > 0) brightness--;
      sendCommand(mode, colorIndex, brightness, param3);
      break;
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n\n============================================================");
  Serial.println("   ESP32 MASTER (LEFT CHANNEL)");
  Serial.println("   IR Remote Controller + Dual Channel Sender");
  Serial.println("============================================================");
  Serial.print("Chip Model: ");
  Serial.println(ESP.getChipModel());
  Serial.print("MAC Address: ");
  Serial.println(WiFi.macAddress());
  Serial.print("Compiled: ");
  Serial.print(__DATE__);
  Serial.print(" ");
  Serial.println(__TIME__);
  Serial.println(String('=', 60) + "\n");
  
  // Initialize WiFi in Station mode
  WiFi.mode(WIFI_STA);
  
  // Set WiFi channel to 6
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(6, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);
  
  // Initialize ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }
  
  Serial.println("ESP-NOW Master ready on channel 6");
  
  // Register peer
  esp_now_peer_info_t peerInfo;
  memset(&peerInfo, 0, sizeof(peerInfo));
  memcpy(peerInfo.peer_addr, slaveMacAddress, 6);
  peerInfo.channel = 6;
  peerInfo.encrypt = false;
  
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add peer");
    return;
  }
  
  // Initialize serial to Teensy
  TeensySerial.begin(TEENSY_SERIAL_BAUD, SERIAL_8N1, TEENSY_RX_PIN, TEENSY_TX_PIN);
  
  Serial.println("\n=== TEENSY SERIAL CONFIGURATION ===");
  Serial.print("TX Pin: ");
  Serial.println(TEENSY_TX_PIN);
  Serial.print("RX Pin: ");
  Serial.println(TEENSY_RX_PIN);
  Serial.print("Baud Rate: ");
  Serial.println(TEENSY_SERIAL_BAUD);
  Serial.println("\nTesting Teensy Serial...");
  
  // Send multiple test messages
  for (int i = 0; i < 3; i++) {
    TeensySerial.println("TEST from ESP32");
    Serial.print("Test message #");
    Serial.print(i+1);
    Serial.println(" sent to Teensy on TX Pin 17");
    delay(100);
  }
  
  // Initialize IR receiver
  irrecv.enableIRIn();
  
  Serial.println("RaveListener V1");
  Serial.println(__FILE__);
  Serial.println("ESP Master ready");
  
  // Send initial state
  sendCommand(mode, colorIndex, brightness, param3);
  Serial.println("Initial command sent to Teensy");
}

void loop() {
  // Check for IR commands
  if (irrecv.decode(&results)) {
    if (results.value != 0) {  // Ignore invalid codes
      handleIRCommand(results.value);
    }
    irrecv.resume();  // Receive the next value
  }
  
  // Small delay to prevent overwhelming the system
  delay(10);
}
