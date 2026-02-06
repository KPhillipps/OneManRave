#include "Globals.h"
#include "ColorDefinitions.h"

// ============================================================================
// Pattern Interruptibility Contract
// ============================================================================
// Patterns are a subcategory of modes and are non-musically reactive in this file.
// This project mixes two styles:
// - Mode 'P' patterns in runPattern() historically use blocking `for (;;)` loops and `delay()`.
// - Mode 'M' visualizations are called once per frame from an outer loop in main.cpp.
//
// When a pattern blocks, the main loop does NOT run, so USB serial commands won't be parsed unless
// the pattern explicitly services input. To avoid "can't be interrupted" regressions:
// - Call patternYield() frequently inside any long loop.
// - Use patternDelay() instead of delay() so we can break immediately on a new command.
// - Prefer frame-based functions `void foo(bool reset=false)` for new patterns.
static inline bool patternYield() {
    return serviceInputs();
}

static inline bool patternDelay(uint16_t ms) {
    return responsiveDelay(ms);
}

void runPattern() {
  static int lastPattern = -1;
  if (state.pattern != lastPattern) {
    DBG_SERIAL_PRINTF("Running pattern %d\n", state.pattern);
    lastPattern = state.pattern;
  }
  switch (state.pattern) {
    case 0:
      break;

    case 1:
      break;

    case 2:
      {                          // Rainbow Pattern with Beat
        static uint8_t hue = 0;  // Global hue, incremented over time
        static float beatFlash = 0.0f;
        uint8_t deltaHue = 7;    // Default spacing for rainbow effect

        for (;;) {
          if (patternYield()) {
            break;  // Exit loop on state change
          }

          // Beat detection - flash and speed up on beat
          if (beatAmplitude > 0.15f) {
            beatFlash = 1.0f;
          }
          beatFlash *= 0.85f;  // Decay

          // Calculate brightness boost from beat
          uint8_t baseBrightness = 180;
          uint8_t beatBoost = (uint8_t)(beatFlash * 75);
          uint8_t brightness = baseBrightness + beatBoost;

          // Fill virtualLeds with a rainbow pattern
          for (int strip = 0; strip < NUM_VIRTUAL_STRIPS; strip++) {
            for (int led = 0; led < LEDS_PER_VIRTUAL_STRIP; led++) {
              *virtualLeds[strip][led] = CHSV(hue + (strip * LEDS_PER_VIRTUAL_STRIP + led) * deltaHue, 255, brightness);
            }
          }

          // Flash outer strips white on strong beats
          if (beatFlash > 0.5f) {
            uint8_t flashVal = (uint8_t)(beatFlash * 255);
            for (int led = 0; led < LEDS_PER_VIRTUAL_STRIP; led++) {
              *virtualLeds[0][led] = CHSV(0, 0, flashVal);
              *virtualLeds[NUM_VIRTUAL_STRIPS - 1][led] = CHSV(0, 0, flashVal);
            }
          }

          FastLED.show();
          // Speed up animation on beat
          int frameDelay = beatFlash > 0.3f ? 10 : 20;
          if (patternDelay(frameDelay)) {
            break;
          }
          hue += beatFlash > 0.3f ? 3 : 1;  // Faster hue shift on beat
        }
      }
      break;

    case 3:
      {  // Rainbow with Sparkle + Beat
        static uint8_t hue = 0;
        static float beatFlash = 0.0f;
        uint8_t deltaHue = 7;

        for (;;) {
          if (patternYield()) {
            break;
          }

          // Beat detection
          if (beatAmplitude > 0.15f) {
            beatFlash = 1.0f;
          }
          beatFlash *= 0.88f;

          // More sparkles on beat (base 50, up to 255 on beat)
          uint8_t sparkleChance = 50 + (uint8_t)(beatFlash * 205);
          uint8_t brightness = 180 + (uint8_t)(beatFlash * 75);

          // Fill virtualLeds with a rainbow pattern
          for (int strip = 0; strip < NUM_VIRTUAL_STRIPS; strip++) {
            for (int led = 0; led < LEDS_PER_VIRTUAL_STRIP; led++) {
              *virtualLeds[strip][led] = CHSV(hue + (strip * LEDS_PER_VIRTUAL_STRIP + led) * deltaHue, 255, brightness);
            }
          }

          // Add sparkles - more on beat
          int numSparkles = beatFlash > 0.5f ? 10 : 2;
          for (int s = 0; s < numSparkles; s++) {
            if (random8() < sparkleChance) {
              int randomStrip = random(NUM_VIRTUAL_STRIPS);
              int randomLed = random(LEDS_PER_VIRTUAL_STRIP);
              *virtualLeds[randomStrip][randomLed] = CRGB::White;
            }
          }

          // Flash outer strips on beat
          if (beatFlash > 0.5f) {
            uint8_t flashVal = (uint8_t)(beatFlash * 255);
            for (int led = 0; led < LEDS_PER_VIRTUAL_STRIP; led++) {
              *virtualLeds[0][led] = CHSV(0, 0, flashVal);
              *virtualLeds[NUM_VIRTUAL_STRIPS - 1][led] = CHSV(0, 0, flashVal);
            }
          }

          FastLED.show();
          if (patternDelay(beatFlash > 0.3f ? 10 : 20)) {
            break;
          }
          hue += beatFlash > 0.3f ? 3 : 1;
        }
      }
      break;

    case 4:
      {  // Fire Pattern with Beat
#define COOLING 100
#define SPARKING 180

        static uint8_t heat[NUM_VIRTUAL_STRIPS][LEDS_PER_VIRTUAL_STRIP] = { { 0 } };
        static float beatFlash = 0.0f;

        for (;;) {
          if (patternYield()) {
            break;
          }

          // Beat detection - flare up on beat
          if (beatAmplitude > 0.15f) {
            beatFlash = 1.0f;
          }
          beatFlash *= 0.90f;

          // More sparking on beat
          uint8_t sparkChance = SPARKING + (uint8_t)(beatFlash * 75);

          for (int strip = 0; strip < NUM_VIRTUAL_STRIPS; strip++) {
            // Step 1: Cool down the heat (less cooling on beat for bigger flames)
            uint8_t coolAmount = beatFlash > 0.3f ? COOLING / 2 : COOLING;
            for (int y = 0; y < LEDS_PER_VIRTUAL_STRIP; y++) {
              heat[strip][y] = qsub8(heat[strip][y], random8(0, ((coolAmount * 10) / LEDS_PER_VIRTUAL_STRIP) + 2));
            }

            // Step 2: Heat diffuses upward
            for (int y = LEDS_PER_VIRTUAL_STRIP - 1; y >= 2; y--) {
              heat[strip][y] = (heat[strip][y - 1] + heat[strip][y - 2] + heat[strip][y - 2]) / 3;
            }

            // Step 3: Ignite new sparks at the bottom - more on beat
            if (random8() < sparkChance) {
              int y = random8(7);
              uint8_t sparkIntensity = beatFlash > 0.5f ? 255 : random8(160, 255);
              heat[strip][y] = qadd8(heat[strip][y], sparkIntensity);
            }

            // Extra beat sparks on outer strips
            if (beatFlash > 0.5f && (strip == 0 || strip == NUM_VIRTUAL_STRIPS - 1)) {
              for (int i = 0; i < 5; i++) {
                int y = random8(LEDS_PER_VIRTUAL_STRIP / 3);
                heat[strip][y] = qadd8(heat[strip][y], 255);
              }
            }

            // Step 4: Map heat to colors
            for (int y = 0; y < LEDS_PER_VIRTUAL_STRIP; y++) {
              *virtualLeds[strip][y] = HeatColor(heat[strip][y]);
            }
          }

          FastLED.show();
          if (patternDelay(5)) {
            break;
          }
        }
      }
      break;

    case 5:
      {  // Sinelon Pattern with Beat
        static uint16_t position[NUM_VIRTUAL_STRIPS] = { 0 };
        static uint8_t hue[NUM_VIRTUAL_STRIPS] = { 0 };
        static float beatFlash = 0.0f;

        for (;;) {
          if (patternYield()) {
            break;  // Exit on Serial1 input
          }

          // Beat detection
          if (beatAmplitude > 0.15f) {
            beatFlash = 1.0f;
          }
          beatFlash *= 0.88f;

          // Clear all LEDs (fade less on beat for longer trails)
          uint8_t fadeAmount = beatFlash > 0.3f ? 30 : 60;
          for (int strip = 0; strip < NUM_VIRTUAL_STRIPS; strip++) {
            for (int led = 0; led < LEDS_PER_VIRTUAL_STRIP; led++) {
              virtualLeds[strip][led]->fadeToBlackBy(fadeAmount);
            }
          }

          // Update each strip with the Sinelon effect
          for (int strip = 0; strip < NUM_VIRTUAL_STRIPS; strip++) {
            hue[strip] += beatFlash > 0.3f ? 16 : 8;  // Faster color change on beat
            position[strip] = beatsin16(13 + strip * 2, 0, LEDS_PER_VIRTUAL_STRIP - 1);

            // Highlight the current position on the strip - brighter on beat
            uint8_t brightness = 180 + (uint8_t)(beatFlash * 75);
            *virtualLeds[strip][position[strip]] = CHSV(hue[strip], 255, brightness);

            // Add extra dot on beat
            if (beatFlash > 0.5f) {
              int mirrorPos = LEDS_PER_VIRTUAL_STRIP - 1 - position[strip];
              *virtualLeds[strip][mirrorPos] = CHSV(hue[strip] + 128, 255, (uint8_t)(beatFlash * 200));
            }
          }

          // Flash outer strips on beat
          if (beatFlash > 0.5f) {
            uint8_t flashVal = (uint8_t)(beatFlash * 255);
            for (int led = 0; led < LEDS_PER_VIRTUAL_STRIP; led++) {
              *virtualLeds[0][led] = CHSV(0, 0, flashVal);
              *virtualLeds[NUM_VIRTUAL_STRIPS - 1][led] = CHSV(0, 0, flashVal);
            }
          }

          FastLED.show();
          if (patternDelay(beatFlash > 0.3f ? 8 : 15)) {
            break;
          }
        }
      }
      break;


    case 6:
      {  // Enhanced Sinelon Pattern with Beat
        static uint16_t position[NUM_VIRTUAL_STRIPS] = { 0 };
        static uint8_t hue[NUM_VIRTUAL_STRIPS] = { 0 };
        static float beatFlash = 0.0f;

        for (;;) {
          if (patternYield()) {
            break;
          }

          // Beat detection
          if (beatAmplitude > 0.15f) {
            beatFlash = 1.0f;
          }
          beatFlash *= 0.88f;

          // Longer comet on beat
          int cometLength = beatFlash > 0.3f ? 35 : 20;

          // Clear LEDs
          FastLED.clear();

          // Update each strip
          for (int strip = 0; strip < NUM_VIRTUAL_STRIPS; strip++) {
            hue[strip] += beatFlash > 0.3f ? 16 : 8;
            position[strip] = beatsin16(13 + strip * 2, 0, LEDS_PER_VIRTUAL_STRIP - 1);

            // Create comet - brighter on beat
            uint8_t baseBrightness = 180 + (uint8_t)(beatFlash * 75);
            for (int offset = 0; offset < cometLength; offset++) {
              int trailPosition = (position[strip] - offset + LEDS_PER_VIRTUAL_STRIP) % LEDS_PER_VIRTUAL_STRIP;
              uint8_t brightness = baseBrightness - (offset * (baseBrightness / cometLength));
              *virtualLeds[strip][trailPosition] += CHSV(hue[strip], 255, brightness);
            }

            // Second comet going opposite direction on beat
            if (beatFlash > 0.5f) {
              int mirrorPos = LEDS_PER_VIRTUAL_STRIP - 1 - position[strip];
              for (int offset = 0; offset < cometLength / 2; offset++) {
                int trailPosition = (mirrorPos + offset) % LEDS_PER_VIRTUAL_STRIP;
                uint8_t brightness = (uint8_t)(beatFlash * 200) - (offset * 10);
                if (brightness > 200) brightness = 0;  // Underflow protection
                *virtualLeds[strip][trailPosition] += CHSV(hue[strip] + 128, 255, brightness);
              }
            }
          }

          // Flash outer strips on beat
          if (beatFlash > 0.5f) {
            uint8_t flashVal = (uint8_t)(beatFlash * 255);
            for (int led = 0; led < LEDS_PER_VIRTUAL_STRIP; led++) {
              *virtualLeds[0][led] = CHSV(0, 0, flashVal);
              *virtualLeds[NUM_VIRTUAL_STRIPS - 1][led] = CHSV(0, 0, flashVal);
            }
          }

          FastLED.show();
          if (patternDelay(beatFlash > 0.3f ? 8 : 15)) {
            break;
          }
        }
      }
      break;
    case 7:
      {  // Meteor Shower with Beat - non-blocking version
        static int meteorPos[NUM_VIRTUAL_STRIPS];
        static uint8_t meteorHue[NUM_VIRTUAL_STRIPS];
        static bool meteorActive[NUM_VIRTUAL_STRIPS];
        static float beatFlash = 0.0f;
        static bool initialized = false;

        if (!initialized) {
          for (int strip = 0; strip < NUM_VIRTUAL_STRIPS; strip++) {
            meteorPos[strip] = -1;
            meteorActive[strip] = false;
            meteorHue[strip] = random8();
          }
          FastLED.clear();
          initialized = true;
        }

        for (;;) {
          if (patternYield()) {
            initialized = false;  // Reset on exit
            break;
          }

          // Beat detection - trigger meteors on beat
          if (beatAmplitude > 0.15f) {
            beatFlash = 1.0f;
            // Launch meteors on random strips on beat
            int numToLaunch = random8(3, 6);
            for (int i = 0; i < numToLaunch; i++) {
              int strip = random8(NUM_VIRTUAL_STRIPS);
              if (!meteorActive[strip]) {
                meteorActive[strip] = true;
                meteorPos[strip] = LEDS_PER_VIRTUAL_STRIP + 10;  // Start above top
                meteorHue[strip] = random8();
              }
            }
          }
          beatFlash *= 0.90f;

          // Randomly spawn meteors when idle
          if (random8() < 15) {
            int strip = random8(NUM_VIRTUAL_STRIPS);
            if (!meteorActive[strip]) {
              meteorActive[strip] = true;
              meteorPos[strip] = LEDS_PER_VIRTUAL_STRIP + 10;
              meteorHue[strip] = random8();
            }
          }

          // Fade all LEDs
          uint8_t fadeAmount = beatFlash > 0.3f ? 30 : 50;
          for (int strip = 0; strip < NUM_VIRTUAL_STRIPS; strip++) {
            for (int led = 0; led < LEDS_PER_VIRTUAL_STRIP; led++) {
              virtualLeds[strip][led]->fadeToBlackBy(fadeAmount);
            }
          }

          // Update and draw meteors
          int meteorSize = beatFlash > 0.3f ? 8 : 5;
          for (int strip = 0; strip < NUM_VIRTUAL_STRIPS; strip++) {
            if (meteorActive[strip]) {
              meteorPos[strip] -= 2;  // Move down

              // Draw meteor with gradient tail
              for (int i = 0; i < meteorSize; i++) {
                int pos = meteorPos[strip] + i;
                if (pos >= 0 && pos < LEDS_PER_VIRTUAL_STRIP) {
                  uint8_t brightness = 255 - (i * (200 / meteorSize));
                  *virtualLeds[strip][pos] = CHSV(meteorHue[strip], 255, brightness);
                }
              }

              // Deactivate when off screen
              if (meteorPos[strip] < -meteorSize) {
                meteorActive[strip] = false;
              }
            }
          }

          // Flash outer strips on beat
          if (beatFlash > 0.5f) {
            uint8_t flashVal = (uint8_t)(beatFlash * 255);
            for (int led = 0; led < LEDS_PER_VIRTUAL_STRIP; led++) {
              *virtualLeds[0][led] = CHSV(0, 0, flashVal);
              *virtualLeds[NUM_VIRTUAL_STRIPS - 1][led] = CHSV(0, 0, flashVal);
            }
          }

          FastLED.show();
          if (patternDelay(beatFlash > 0.3f ? 15 : 25)) {
            initialized = false;
            break;
          }
        }
      }
      break;
    default:
      break;
  }
}
