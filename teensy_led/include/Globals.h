#ifndef GLOBAL_STATE_H
#define GLOBAL_STATE_H

#include <FastLED.h>

// ============================================================================
// Debug Controls
// ============================================================================
#ifndef DEBUG_SERIAL
#define DEBUG_SERIAL 0
#endif
#ifndef DEBUG_STATUS
#define DEBUG_STATUS 1
#endif

#if DEBUG_SERIAL
#define DBG_SERIAL_PRINT(...) Serial.print(__VA_ARGS__)
#define DBG_SERIAL_PRINTLN(...) Serial.println(__VA_ARGS__)
#define DBG_SERIAL_PRINTF(...) Serial.printf(__VA_ARGS__)
#else
#define DBG_SERIAL_PRINT(...) do {} while (0)
#define DBG_SERIAL_PRINTLN(...) do {} while (0)
#define DBG_SERIAL_PRINTF(...) do {} while (0)
#endif

#if DEBUG_STATUS
#define STAT_PRINT(...) Serial.print(__VA_ARGS__)
#define STAT_PRINTLN(...) Serial.println(__VA_ARGS__)
#else
#define STAT_PRINT(...) do {} while (0)
#define STAT_PRINTLN(...) do {} while (0)
#endif

// ============================================================================
// LED Configuration
// ============================================================================
#define NUM_APA102_STRIPS 6                  // 6 physical APA102 strips
#define LEDS_PER_VIRTUAL_STRIP 144           // LEDs per virtual strip
#define LEDS_PER_PHYSICAL_STRIP 288          // LEDs per physical strip
#define NUM_PHYSICAL_STRIPS 6                // Total physical strips
#define NUM_VIRTUAL_STRIPS 12                // Virtual strips (each physical → 2 virtual)
#define MAX_BANDS 12                         // Maximum FFT frequency bands
#define BANDS_12 12                          // 12 bands for M patterns 0-5
#define BANDS_10 10                          // Legacy: unused (kept for compatibility)

// ============================================================================
// Pin Configuration
// ============================================================================
const int CLOCK_PIN = 14;  // Shared SPI clock for all APA102 strips
const int BUFFER_ENABLE = 3;  // 74HCT245 OE pin (active high)

// Data pins for each strip (must match FastLED.addLeds order in main.cpp)
constexpr int STRIP_PINS[6] = {6, 7, 8, 2, 21, 5};

// ============================================================================
// Global LED Arrays (extern - defined in main.cpp)
// ============================================================================
extern CRGB* virtualLeds[NUM_VIRTUAL_STRIPS][LEDS_PER_VIRTUAL_STRIP];
extern float bandAmplitude[MAX_BANDS];  // Smoothed FFT band data from FFT Teensy
extern int currentBandCount;            // Current active band count (10 or 12)
extern float beatAmplitude;         // Disabled (no beat data in payload)
extern uint8_t vocalEnv;            // 0-255 vocal envelope
extern uint8_t vocalSyllable;       // 0/1 syllable hit
extern uint8_t vocalNote;           // 0-11 pitch class, 255=none
extern uint8_t vocalNoteStrength;   // 0-255
extern uint8_t vocalSustain;        // 0/1 sustain flag
extern uint8_t spdifLock;           // SPDIF lock status from FFT Teensy

// AUX audio features (from FFT Teensy)
extern uint8_t bandVis8[MAX_BANDS];    // 0-255 per band (log-compressed)
extern uint8_t bandDelta8[MAX_BANDS];  // 0-255 per band positive delta
extern uint8_t globalVis8;             // 0-255 overall energy
extern uint8_t bassVis8;               // 0-255 bass energy
extern uint8_t midVis8;                // 0-255 mid energy
extern uint8_t trebleVis8;             // 0-255 treble energy
extern uint16_t majorPeakHz;           // Major peak frequency (Hz)
extern uint8_t majorPeakMag;           // 0-255 peak magnitude
extern uint8_t spectralFlux8;          // 0-255 spectral flux
extern uint8_t peakDetected;           // 0/1 peak gate
extern unsigned long lastAuxPacketMs;  // Timestamp of last AUX frame

// ============================================================================
// State Structure (mode/pattern/brightness)
// ============================================================================
struct State {
    char mode;         // '0'=Off, 'S'=Solid, 'P'=Pattern, 'M'=Music, 'A'=Art
    int pattern;       // S=Color index, M=Visualization, P=Pattern, A=Art
    int brightness;    // LED brightness (0-255)
};

extern State state;
extern bool firstRun;
extern volatile bool serialDataPending;  // Set by serialEvent callbacks

// Forward declarations used by the inline helpers below
void processSerialData();
void processUsbSerialCommands();

// ============================================================================
// Input Service Helpers (Interruptible Patterns)
// ============================================================================
// IMPORTANT:
// When an effect/pattern uses a blocking loop (or delay), the main loop won't run and USB serial
// commands won't be parsed unless the effect explicitly services input. Use these helpers inside
// long loops so mode/pattern changes interrupt immediately.
static inline bool serviceInputs() {
    processUsbSerialCommands();
    processSerialData();
    return serialDataPending;
}

static inline bool responsiveDelay(uint16_t ms) {
    uint32_t start = millis();
    while ((uint32_t)(millis() - start) < ms) {
        if (serviceInputs()) {
            return true;
        }
        delay(1);
    }
    return false;
}

// ============================================================================
// Function Declarations
// ============================================================================
void runPattern();
void showArt();
int getBandCountForPattern(int pattern);  // Returns 12 for patterns 0-5, 10 for 6-11

#endif
