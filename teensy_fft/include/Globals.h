#ifndef GLOBAL_STATE_H
#define GLOBAL_STATE_H

#include <FastLED.h>

// ============================================================================
// LED Configuration
// ============================================================================
#define NUM_APA102_STRIPS 6                  // 6 physical APA102 strips
#define NUM_WS2812_STRIPS 0                  // No WS2812 strips (all APA102)
#define LEDS_PER_VIRTUAL_STRIP 144           // LEDs per virtual strip
#define LEDS_PER_PHYSICAL_STRIP 288          // LEDs per physical strip
#define NUM_PHYSICAL_STRIPS 6                // Total physical strips
#define NUM_VIRTUAL_STRIPS 12                // Virtual strips (each physical → 2 virtual)
#define BANDS 10                             // FFT frequency bands

// ============================================================================
// Pin Configuration
// ============================================================================
const int CLOCK_PIN = 13;  // Shared SPI clock for all APA102 strips

// ============================================================================
// Global LED Arrays (extern - defined in main.cpp)
// ============================================================================
extern CRGB* virtualLeds[NUM_VIRTUAL_STRIPS][LEDS_PER_VIRTUAL_STRIP];

// ============================================================================
// State Structure (mode/pattern/color/brightness)
// ============================================================================
struct State {
    char mode;         // '0'=Off, 'S'=Solid, 'P'=Pattern, 'M'=Music, 'A'=Art
    int pattern;       // Pattern number (also used as color index)
    int brightness;    // LED brightness (0-255)
};

extern State state;
extern bool firstRun;

// ============================================================================
// Function Declarations
// ============================================================================
void runPattern();
void showArt();

#endif
