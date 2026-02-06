// ============================================================================
// ESP32 MASTER - TEENSY SERIAL TEST
// ============================================================================
// Simple test to verify serial communication to Teensy
// Replace main.cpp temporarily with this to test
//
// Expected: Sends "TEST" every 2 seconds to Teensy
// ============================================================================

#include <Arduino.h>
#include <HardwareSerial.h>

#define TEENSY_TX_PIN 17
#define TEENSY_RX_PIN 18
#define TEENSY_SERIAL_BAUD 38400

HardwareSerial TeensySerial(1);

void setup() {
    Serial.begin(115200);
    delay(2000);
    
    Serial.println("\n=== ESP32 TEENSY SERIAL TEST ===");
    Serial.println("Initializing Serial to Teensy...");
    
    TeensySerial.begin(TEENSY_SERIAL_BAUD, SERIAL_8N1, TEENSY_RX_PIN, TEENSY_TX_PIN);
    
    Serial.printf("TX Pin %d -> Teensy RX Pin 7\n", TEENSY_TX_PIN);
    Serial.printf("RX Pin %d <- Teensy TX Pin 8\n", TEENSY_RX_PIN);
    Serial.printf("Baud: %d\n", TEENSY_SERIAL_BAUD);
    Serial.println("\nSending test messages...\n");
}

void loop() {
    static unsigned long lastSend = 0;
    static int count = 0;
    
    // Send test message every 2 seconds
    if (millis() - lastSend > 2000) {
        lastSend = millis();
        count++;
        
        char buffer[32];
        sprintf(buffer, "TEST %d\n", count);
        TeensySerial.print(buffer);
        
        Serial.print("Sent to Teensy: ");
        Serial.print(buffer);
    }
    
    // Echo anything received from Teensy
    if (TeensySerial.available()) {
        char c = TeensySerial.read();
        Serial.print("Received from Teensy: ");
        Serial.print(c);
        Serial.print(" (0x");
        Serial.print((int)c, HEX);
        Serial.println(")");
    }
}
