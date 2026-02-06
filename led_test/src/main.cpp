#include <Arduino.h>
#include <FastLED.h>

// APA102 test across Octo data pins with shared clock on pin 14.
// Data pins (no 14): 2,7,8,6 on left jack; 20,21,5 on right jack.

#define NUM_STRIPS 7
#define LEDS_PER_STRIP 288
#define CLOCK_PIN 14
#define BUFFER_ENABLE 3 // 74HCT245 OE pin (active high)

const uint8_t DATA_PINS[NUM_STRIPS] = {2, 7, 8, 6, 20, 21, 5};

CRGB leds[NUM_STRIPS * LEDS_PER_STRIP];

void setup() {
    pinMode(BUFFER_ENABLE, OUTPUT);
    digitalWrite(BUFFER_ENABLE, HIGH);  // enable buffer so RJ45 sees the signal.

    // Register each strip with shared clock 14
    FastLED.addLeds<APA102, 2,  CLOCK_PIN, BGR>(leds + (0 * LEDS_PER_STRIP), LEDS_PER_STRIP);
    FastLED.addLeds<APA102, 7,  CLOCK_PIN, BGR>(leds + (1 * LEDS_PER_STRIP), LEDS_PER_STRIP);
    FastLED.addLeds<APA102, 8,  CLOCK_PIN, BGR>(leds + (2 * LEDS_PER_STRIP), LEDS_PER_STRIP);
    FastLED.addLeds<APA102, 6,  CLOCK_PIN, BGR>(leds + (3 * LEDS_PER_STRIP), LEDS_PER_STRIP);
    FastLED.addLeds<APA102, 20, CLOCK_PIN, BGR>(leds + (4 * LEDS_PER_STRIP), LEDS_PER_STRIP);
    FastLED.addLeds<APA102, 21, CLOCK_PIN, BGR>(leds + (5 * LEDS_PER_STRIP), LEDS_PER_STRIP);
    FastLED.addLeds<APA102, 5,  CLOCK_PIN, BGR>(leds + (6 * LEDS_PER_STRIP), LEDS_PER_STRIP);

    FastLED.setBrightness(20);
}

void loop() {
    static uint8_t hue = 0;
    const int total = NUM_STRIPS * LEDS_PER_STRIP;
    fill_rainbow(leds, total, hue, 3);
    FastLED.show();
    hue += 2;
    delay(20);
}