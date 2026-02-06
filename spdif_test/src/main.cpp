#include <Audio.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <SerialFlash.h>

// Audio system objects (SPDIF-in on pin 15 / RX)
AudioInputSPDIF3 spdifIn;      // Optical/coax SPDIF input
AudioAnalyzePeak peakL, peakR; // Peak detectors

AudioConnection patchCord1(spdifIn, 0, peakL, 0);
AudioConnection patchCord2(spdifIn, 1, peakR, 0);

// Settings
const float ledThreshold = 0.001f; // Lower to show very small signals
const int ledPin = 13;             // Teensy 4.0 built-in LED

void setup() {
    pinMode(ledPin, OUTPUT);
    Serial.begin(115200);
    
    while (!Serial && millis() < 4000) {
        // wait for Serial Monitor to attach (optional)
    }
    
    AudioMemory(20);
    Serial.println("\n=== Teensy SPDIF Test (Pin 15) ===");
    Serial.println("Checking for lock and audio levels...\n");
}

void loop() {
    uint32_t now = millis();
    
    // Heartbeat status every second
    static uint32_t lastStatus = 0;
    if (now - lastStatus > 1000) {
        lastStatus = now;
        
        bool locked = spdifIn.pllLocked();
        uint32_t sr = spdifIn.sampleRate();
        
        Serial.print("[STATUS] SPDIF Lock: ");
        Serial.print(locked ? "YES" : "NO");
        Serial.print("  Sample Rate: ");
        Serial.print(sr);
        Serial.println(" Hz");
    }
    
    // Read and display audio peaks
    if (peakL.available() && peakR.available()) {
        float L = peakL.read(); // 0.0–1.0
        float R = peakR.read();
        
        bool active = (L > ledThreshold) || (R > ledThreshold);
        digitalWrite(ledPin, active ? HIGH : LOW);
        
        static uint32_t lastPrint = 0;
        if (now - lastPrint > 100) { // print every ~100 ms
            lastPrint = now;
            
            Serial.print("Peak L: ");
            Serial.print(L, 4);
            Serial.print("  R: ");
            Serial.print(R, 4);
            
            // Simple bar graph
            Serial.print("  |");
            int bars = (int)(max(L, R) * 50);
            for (int i = 0; i < bars && i < 50; i++) Serial.print("=");
            Serial.println("|");
        }
    }
}
