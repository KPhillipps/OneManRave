// ============================================================================
// TEENSY FFT - SERIAL2 TEST
// ============================================================================
// Simple test to verify Serial2 is receiving data from ESP32
// Upload this to test the connection before running the full FFT code
//
// Expected: Anything sent to Serial2 Pin 7 will be echoed to USB Serial
// ============================================================================

void setup() {
    Serial.begin(115200);
    Serial2.begin(38400);  // Serial2 RX Pin 7, TX Pin 8
    delay(2000);
    
    Serial.println("\n=== TEENSY SERIAL2 TEST ===");
    Serial.println("Serial2 RX Pin 7 @ 38400 baud");
    Serial.println("Waiting for data from ESP32 TX Pin 17...");
    Serial.println("Connect:");
    Serial.println("  ESP32 TX Pin 17 -> Teensy RX Pin 7");
    Serial.println("  ESP32 GND       -> Teensy GND");
    Serial.println("\nListening...\n");
}

void loop() {
    // Echo anything received on Serial2 to USB Serial
    if (Serial2.available()) {
        char c = Serial2.read();
        Serial.print("Received: ");
        Serial.print(c);
        Serial.print(" (0x");
        Serial.print((int)c, HEX);
        Serial.println(")");
    }
    
    // Also send a test byte every 5 seconds
    static unsigned long lastTest = 0;
    if (millis() - lastTest > 5000) {
        lastTest = millis();
        Serial2.write('T');  // Send test byte to ESP32 (if RX connected)
        Serial.println("Sent test byte 'T' on Serial2 TX Pin 8");
    }
}
