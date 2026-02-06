#include <stdint.h>
#include "Globals.h"

void displayBitmap(const uint32_t* bitmap) {
    for (int stripIndex = 0; stripIndex < NUM_VIRTUAL_STRIPS; stripIndex++) {
        for (int ledIndex = 0; ledIndex < LEDS_PER_VIRTUAL_STRIP; ledIndex++) {
            // Access the flattened bitmap array using the strip and LED indices
            uint32_t color = bitmap[stripIndex * LEDS_PER_VIRTUAL_STRIP + ledIndex];
            *virtualLeds[stripIndex][ledIndex] = CRGB((color >> 16) & 0xFF,  // Extract red
                                                       (color >> 8) & 0xFF,   // Extract green
                                                       color & 0xFF);         // Extract blue
        }
    }
    FastLED.show();
}

void addPerlinNoise(uint16_t x, uint16_t y, uint8_t scale) {
    for (int strip = 0; strip < NUM_VIRTUAL_STRIPS; strip++) {
        for (int led = 0; led < LEDS_PER_VIRTUAL_STRIP; led++) {
            // Generate Perlin noise brightness
            uint8_t noiseBrightness = inoise8(x + strip * scale, y + led * scale);

            // Convert CHSV to CRGB and add to the existing color
            CRGB color = CHSV(0, 0, noiseBrightness);
            *virtualLeds[strip][led] += color; // Add the converted CRGB
        }
    }
    FastLED.show();
}

void showArt() {  // Control is retained in a while loop until the state changes

  switch (state.pattern) {
    case 0:

      for (;;) {  // Retain control in a loop until the pattern/state changes
        // IMPORTANT: Don't just check Serial1.available() here.
        // FFT frames are continuous and would cause immediate exits, and USB commands
        // wouldn't be processed. Instead, service inputs and break only on parsed commands.
        if (serviceInputs()) {
          break;
        }

        // Add noise effect

      
        FastLED.show();
        if (responsiveDelay(100)) {
          break;
        }

      }
      break;

    case 1:
      // Future pattern handling
      break;

    case 2:
      // Future pattern handling
      break;
  }
}
