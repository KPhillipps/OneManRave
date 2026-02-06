#ifndef COLORDEFINITIONS_H
#define COLORDEFINITIONS_H

#include <FastLED.h>

// Array of predefined colors, indexed for consistency
const CRGB colorOptions[] = {
    CRGB::White,     // 0
    CRGB::Red,       // 1
    CRGB::Green,     // 2
    CRGB::Blue,      // 3
    CRGB::Yellow,    // 4
    CRGB::Cyan,      // 5
    CRGB::Magenta,   // 6
    CRGB::Orange,    // 7
    CRGB::Purple,    // 8
    CRGB::Pink,      // 9
    CRGB::Gold,      // 10
    CRGB::Silver     // 11
};

constexpr int COLOR_OPTIONS_COUNT = sizeof(colorOptions) / sizeof(colorOptions[0]);

#endif
