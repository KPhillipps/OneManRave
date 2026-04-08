#include <Arduino.h>
#include <FastLED.h>

constexpr uint8_t kClockPin = 14;
constexpr uint16_t kNumLeds = 144;
constexpr uint8_t kBrightness = 16;
constexpr uint16_t kOnMs = 150;
constexpr uint16_t kOffMs = 50;

CRGB leds2[kNumLeds];
CRGB leds7[kNumLeds];
CRGB leds8[kNumLeds];
CRGB leds6[kNumLeds];
CRGB leds20[kNumLeds];
CRGB leds21[kNumLeds];
CRGB leds5[kNumLeds];

struct PinTest {
    uint8_t dataPin;
    CRGB* leds;
};

// Octo adapter signal pins on Teensy 4.0.
// Pin 14 is reserved as APA102 clock, so test the remaining output pins as data.
PinTest tests[] = {
    {2, leds2}, {7, leds7}, {8, leds8}, {6, leds6},
    {20, leds20}, {21, leds21}, {5, leds5},
};

constexpr uint8_t kNumTests = sizeof(tests) / sizeof(tests[0]);

void setup() {
    FastLED.addLeds<APA102, 2, kClockPin, BGR, DATA_RATE_MHZ(10)>(leds2, kNumLeds);
    FastLED.addLeds<APA102, 7, kClockPin, BGR, DATA_RATE_MHZ(10)>(leds7, kNumLeds);
    FastLED.addLeds<APA102, 8, kClockPin, BGR, DATA_RATE_MHZ(10)>(leds8, kNumLeds);
    FastLED.addLeds<APA102, 6, kClockPin, BGR, DATA_RATE_MHZ(10)>(leds6, kNumLeds);
    FastLED.addLeds<APA102, 20, kClockPin, BGR, DATA_RATE_MHZ(10)>(leds20, kNumLeds);
    FastLED.addLeds<APA102, 21, kClockPin, BGR, DATA_RATE_MHZ(10)>(leds21, kNumLeds);
    FastLED.addLeds<APA102, 5, kClockPin, BGR, DATA_RATE_MHZ(10)>(leds5, kNumLeds);
    FastLED.setBrightness(kBrightness);
    FastLED.clear(true);
}

void loop() {
    static uint8_t activeTest = 0;

    for (uint8_t i = 0; i < kNumTests; ++i) {
        fill_solid(tests[i].leds, kNumLeds, CRGB::Black);
    }

    fill_solid(tests[activeTest].leds, kNumLeds, CRGB::Blue);
    FastLED.show();
    delay(kOnMs);

    fill_solid(tests[activeTest].leds, kNumLeds, CRGB::Red);
    FastLED.show();
    delay(kOnMs);

    FastLED.clear(true);
    delay(kOffMs);

    activeTest = (activeTest + 1) % kNumTests;
}
