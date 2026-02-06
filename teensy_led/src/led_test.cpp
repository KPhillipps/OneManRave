// ============================================================================
// Standalone LED Test - Snake up/down all 6 APA102 strips
// ============================================================================
// To use: rename main.cpp temporarily, then build and upload this file
// Or create a separate PlatformIO environment for testing
// ============================================================================

#include <Arduino.h>
#include <FastLED.h>

#define NUM_STRIPS 6
#define LEDS_PER_STRIP 288
#define TOTAL_LEDS (NUM_STRIPS * LEDS_PER_STRIP)
#define CLOCK_PIN 13

CRGB leds[TOTAL_LEDS];

void snakeTest() {
    const int SNAKE_LEN = 10;
    const int SPEED_MS = 8;

    // Go up
    for (int pos = 0; pos <= LEDS_PER_STRIP + SNAKE_LEN; pos++) {
        fill_solid(leds, TOTAL_LEDS, CRGB::Black);
        for (int strip = 0; strip < NUM_STRIPS; strip++) {
            int baseIndex = strip * LEDS_PER_STRIP;
            for (int i = 0; i < SNAKE_LEN; i++) {
                int ledPos = pos - i;
                if (ledPos >= 0 && ledPos < LEDS_PER_STRIP) {
                    uint8_t brightness = 255 - (i * 25);
                    leds[baseIndex + ledPos] = CHSV(strip * 40, 255, brightness);
                }
            }
        }
        FastLED.show();
        delay(SPEED_MS);
    }

    // Go down
    for (int pos = LEDS_PER_STRIP - 1; pos >= -SNAKE_LEN; pos--) {
        fill_solid(leds, TOTAL_LEDS, CRGB::Black);
        for (int strip = 0; strip < NUM_STRIPS; strip++) {
            int baseIndex = strip * LEDS_PER_STRIP;
            for (int i = 0; i < SNAKE_LEN; i++) {
                int ledPos = pos + i;
                if (ledPos >= 0 && ledPos < LEDS_PER_STRIP) {
                    uint8_t brightness = 255 - (i * 25);
                    leds[baseIndex + ledPos] = CHSV(strip * 40, 255, brightness);
                }
            }
        }
        FastLED.show();
        delay(SPEED_MS);
    }

    fill_solid(leds, TOTAL_LEDS, CRGB::Black);
    FastLED.show();
}

void allWhite() {
    fill_solid(leds, TOTAL_LEDS, CRGB::White);
    FastLED.show();
    delay(1000);
    fill_solid(leds, TOTAL_LEDS, CRGB::Black);
    FastLED.show();
}

void stripByStrip() {
    CRGB colors[] = {CRGB::Red, CRGB::Green, CRGB::Blue, CRGB::Yellow, CRGB::Magenta, CRGB::Cyan};

    for (int strip = 0; strip < NUM_STRIPS; strip++) {
        fill_solid(leds, TOTAL_LEDS, CRGB::Black);
        int baseIndex = strip * LEDS_PER_STRIP;
        for (int i = 0; i < LEDS_PER_STRIP; i++) {
            leds[baseIndex + i] = colors[strip];
        }
        FastLED.show();
        Serial.printf("Strip %d: %s (LEDs %d-%d)\n", strip,
            strip == 0 ? "Red" : strip == 1 ? "Green" : strip == 2 ? "Blue" :
            strip == 3 ? "Yellow" : strip == 4 ? "Magenta" : "Cyan",
            baseIndex, baseIndex + LEDS_PER_STRIP - 1);
        delay(1000);
    }

    fill_solid(leds, TOTAL_LEDS, CRGB::Black);
    FastLED.show();
}

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("=== LED Test Program ===");
    Serial.println("Initializing 6 APA102 strips...");

    FastLED.addLeds<APA102, 2, CLOCK_PIN, BGR>(leds, 288).setCorrection(TypicalLEDStrip);
    FastLED.addLeds<APA102, 3, CLOCK_PIN, BGR>(leds + 288, 288).setCorrection(TypicalLEDStrip);
    FastLED.addLeds<APA102, 4, CLOCK_PIN, BGR>(leds + 576, 288).setCorrection(TypicalLEDStrip);
    FastLED.addLeds<APA102, 5, CLOCK_PIN, BGR>(leds + 864, 288).setCorrection(TypicalLEDStrip);
    FastLED.addLeds<APA102, 16, CLOCK_PIN, BGR>(leds + 1152, 288).setCorrection(TypicalLEDStrip);
    FastLED.addLeds<APA102, 18, CLOCK_PIN, BGR>(leds + 1440, 288).setCorrection(TypicalLEDStrip);

    FastLED.setMaxPowerInMilliWatts(250000);
    FastLED.setBrightness(100);

    Serial.println("Ready. Commands: s=snake, w=white flash, c=strip colors, +=bright, -=dim");
}

void loop() {
    if (Serial.available()) {
        char cmd = Serial.read();

        switch (cmd) {
            case 's':
                Serial.println("Running snake test...");
                snakeTest();
                Serial.println("Done.");
                break;

            case 'w':
                Serial.println("White flash...");
                allWhite();
                break;

            case 'c':
                Serial.println("Strip-by-strip color test...");
                stripByStrip();
                break;

            case '+':
            case '=':
                FastLED.setBrightness(min(255, FastLED.getBrightness() + 25));
                Serial.printf("Brightness: %d\n", FastLED.getBrightness());
                break;

            case '-':
                FastLED.setBrightness(max(10, FastLED.getBrightness() - 25));
                Serial.printf("Brightness: %d\n", FastLED.getBrightness());
                break;
        }

        while (Serial.available()) Serial.read();
    }
}
