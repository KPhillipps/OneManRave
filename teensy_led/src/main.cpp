#include <Arduino.h>
extern "C" char usb_string_serial_number[];
#include "Globals.h"

// Uncomment to run LED strip tests on startup
// #define DEBUG_LED_TEST

#define FASTLED_SPI_SPEED 12  // 12MHz SPI for APA102
#include <FastLED.h>
#include "ColorDefinitions.h"
#include "Patterns.h"
#include "MusicVisualization.h"
#include <EEPROM.h>
#include "Art.h"

// ============================================================================
// RaveGPT - Teensy B: LED Display Controller (SLAVE)
// ============================================================================
// This is a SLAVE device - NO LOCAL UI OR KEYBOARD INPUT.
// USB Serial is for debug output ONLY.
//
// ========================== SERIAL1 PROTOCOL ==========================
// Serial1 @ 460800 baud (binary-only):
// Frame: [0xAA][type][seq][len][payload][crc16][0xBB]
// - type=0x01: FFT payload (68 bytes): 12 floats + vocal bytes + spdif + chroma[12] + dominantPitch + pitchStrength + reserved
// - type=0x02: CMD payload (68 bytes): mode, pattern, pattern, brightness, then padding
// - type=0x03: AUX payload (36 bytes): bandVis/bandDelta + peak + flux + derived metrics
//
// ========================== MODES ==========================
//   '0' = Off (all black)
//   'S' = Solid color (uses pattern -> colorOptions[])
//   'P' = Pattern animation (pattern 2-10, see Patterns.cpp)
//   'M' = Music visualization (uses FFT data)
//   'A' = Art/bitmap display
// ============================================================================

// Global LED Arrays
CRGB leds[1728];    // Physical array: 6 APA102 strips × 288 LEDs
CRGB* virtualLeds[NUM_VIRTUAL_STRIPS][LEDS_PER_VIRTUAL_STRIP];  // 12 virtual strips, each LED mapped individually
constexpr int LEDL = NUM_APA102_STRIPS * LEDS_PER_PHYSICAL_STRIP;

// State (mode/pattern/brightness)
// Default: Solid color mode - don't auto-launch animations
State state = {'S', 0, 10};  // Default: Solid color, color index 0 (white), brightness 10

// Stored settings per mode - restored when switching modes
struct ModeSettings {
    int pattern;      // Pattern/color index
    int brightness;   // Brightness level
};
ModeSettings savedMusic = {0, 10};    // Music mode: visualization 0, brightness 10
ModeSettings savedSolid = {0, 10};    // Solid mode: color 0, brightness 10
ModeSettings savedPattern = {0, 10};  // Pattern mode: pattern 0 (rainbow), brightness 10

bool firstRun = false;
bool message = true;
volatile bool serialDataPending = false;  // Set TRUE when a command is parsed, patterns check this

// ============================================================================
// Serial1 Receive Buffer - Framed Protocol with CRC
// ============================================================================
const uint32_t BAUD_RATE = 460800;  // Must match FFT Teensy Serial2
const int SERIAL_PORT = 1;  // Serial1 (pins 1/0 RX/TX)

// Protocol constants (must match FFT Teensy)
namespace Proto {
    static const uint8_t SOF = 0xAA;
    static const uint8_t EOF_BYTE = 0xBB;
    static const uint8_t TYPE_FFT = 0x01;
    static const uint8_t TYPE_CMD = 0x02;
    static const uint8_t TYPE_AUX = 0x03;
    // FFT payload: 12 floats (48) + vocal bytes (4) + spdif (1) + chroma (12) + dominantPitch (1) + pitchStrength (1) + reserved (1) = 68 bytes
    static const uint8_t FFT_PAYLOAD_LEN = 68;
    static const uint8_t AUX_PAYLOAD_LEN = 36;
    static const uint8_t FIXED_PAYLOAD_LEN = 68;   // all frames padded to 68 bytes
    static const size_t MAX_FRAME_SIZE = 80;       // 4 header + 68 payload + 3 CRC/EOF + margin

    inline uint16_t crc16_ccitt(const uint8_t* data, size_t len) {
        uint16_t crc = 0xFFFF;
        for (size_t i = 0; i < len; ++i) {
            crc ^= static_cast<uint16_t>(data[i]) << 8;
            for (int b = 0; b < 8; ++b) {
                crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : (crc << 1);
            }
        }
        return crc;
    }
}

uint8_t rxBuffer[Proto::MAX_FRAME_SIZE];
int rxIndex = 0;

// FFT data received from Teensy A (already smoothed)
float bandAmplitude[MAX_BANDS] = {0};
float beatAmplitude = 0.0f;  // Disabled (no beat data in payload)
uint8_t vocalEnv = 0;        // 0-255
uint8_t vocalSyllable = 0;   // 0/1
uint8_t vocalNote = 255;     // 0-11, 255=none
uint8_t vocalNoteStrength = 0;
uint8_t vocalSustain = 0;    // 0/1
uint8_t spdifLock = 0;
int currentBandCount = BANDS_12;  // Default to 12 bands

// AUX audio features (from FFT Teensy)
uint8_t bandVis8[MAX_BANDS] = {0};
uint8_t bandDelta8[MAX_BANDS] = {0};
uint8_t globalVis8 = 0;
uint8_t bassVis8 = 0;
uint8_t midVis8 = 0;
uint8_t trebleVis8 = 0;
uint16_t majorPeakHz = 0;
uint8_t majorPeakMag = 0;
uint8_t spectralFlux8 = 0;
uint8_t peakDetected = 0;
unsigned long lastAuxPacketMs = 0;

// Chroma/pitch data (for future use)
uint8_t chroma[12] = {0};        // Pitch class energies: C, C#, D, D#, E, F, F#, G, G#, A, A#, B
uint8_t dominantPitch = 255;     // 0-11 = pitch class, 255 = none
uint8_t pitchStrength = 0;       // 0-255
uint8_t pitchReserved = 0;       // reserved

// Frame timing
unsigned long lastFrameTime = 0;
unsigned long frameCount = 0;
#if DEBUG_STATUS
unsigned long frameWorkAccumUs = 0;
unsigned long frameWorkMaxUs = 0;
unsigned long cpuWindowStartMs = 0;
#endif
const unsigned long FRAME_PERIOD_MS = 17;  // ~60 FPS
unsigned long lastPacketTime = 0;

// ============================================================================
// Serial1 Receiver - Binary FFT
// ============================================================================
static const char* patternLabelForMode(char mode) {
    switch (mode) {
        case 'S': return "Color";
        case 'M': return "Viz";
        case 'P': return "Pattern";
        default:  return "Pattern";
    }
}

static void printControlStatus(const char* tag, char mode, int pattern, int brightness) {
    DBG_SERIAL_PRINTF("%s Mode:%c %s:%d Brt:%d\n", tag, mode, patternLabelForMode(mode), pattern, brightness);
}

// Forward declaration (used by applyCommandPayload)
static void switchToMode(char newMode, int pattern, int brightness);

// Fire tuning defaults (used by USB F command and fire renderer)

// Persisted fire tuning (EEPROM)
static const uint32_t FIRE_SETTINGS_MAGIC = 0xF1DE0A01;
struct FireSettings {
    uint32_t magic;
    float audioBoost;
    uint8_t cooling;
    uint8_t sparking;
};

static void loadFireSettings() {
    FireSettings s;
    EEPROM.get(0, s);
    if (s.magic == FIRE_SETTINGS_MAGIC) {
        setFireParams(s.audioBoost, (uint8_t)s.cooling, (uint8_t)s.sparking);
    }
}

static void saveFireSettings() {
    FireSettings s;
    s.magic = FIRE_SETTINGS_MAGIC;
    getFireParams(s.audioBoost, s.cooling, s.sparking);
    EEPROM.put(0, s);
}

// Apply a TYPE_CMD payload (shared by Serial1 and USB commands)
static void applyCommandPayload(const uint8_t* payload) {
    // Command payload (68 bytes):
    // 0: mode, 1: pattern, 3: brightness
    char newMode = payload[0];
    int newPattern = payload[1];
    int newBrightness = payload[3];

    switchToMode(newMode, newPattern, newBrightness);
    printControlStatus("[RX CMD]", state.mode, state.pattern, state.brightness);
    Serial.printf("[CMD] mode=%c pattern=%d brightness=%d\n", state.mode, state.pattern, state.brightness);
}

// Parse and apply USB CSV line: "M,pattern,brightness"
static void parseUsbCommandLine(const char* line) {
    // Fire tuning command: "F,boost,cooling,sparking"
    if (line[0] == 'F' || line[0] == 'f') {
        float boost = 0.0f;
        int cooling = 0;
        int sparking = 0;
        if (sscanf(line, " %*c,%f,%d,%d", &boost, &cooling, &sparking) == 3) {
            setFireParams(boost, (uint8_t)cooling, (uint8_t)sparking);
            saveFireSettings();
            float curBoost = 0.0f;
            uint8_t curCooling = 0;
            uint8_t curSparking = 0;
            getFireParams(curBoost, curCooling, curSparking);
            DBG_SERIAL_PRINTF("[USB FIRE] boost=%.2f cooling=%u spark=%u\n", curBoost, curCooling, curSparking);
        } else {
            DBG_SERIAL_PRINTLN("[USB FIRE] Parse error");
        }
        return;
    }

    char mode = 0;
    int p1 = 0;
    int p3 = 0;

    int parsed = sscanf(line, " %c,%d,%d", &mode, &p1, &p3);
    if (parsed != 3) {
        DBG_SERIAL_PRINTLN("[USB CMD] Parse error");
        return;
    }

    if (mode == '0' || mode == 'S' || mode == 'P' || mode == 'M' || mode == 'A') {
        uint8_t payload[Proto::FIXED_PAYLOAD_LEN] = {0};
        payload[0] = static_cast<uint8_t>(mode);
        payload[1] = static_cast<uint8_t>(p1);
        payload[3] = static_cast<uint8_t>(p3);
        applyCommandPayload(payload);
    } else {
        DBG_SERIAL_PRINTLN("[USB CMD] Invalid mode");
    }
}

// Save current mode settings before switching
static void saveCurrentModeSettings() {
    switch (state.mode) {
        case 'M':
            savedMusic.pattern = state.pattern;
            savedMusic.brightness = state.brightness;
            break;
        case 'S':
            savedSolid.pattern = state.pattern;
            savedSolid.brightness = state.brightness;
            break;
        case 'P':
            savedPattern.pattern = state.pattern;
            savedPattern.brightness = state.brightness;
            break;
    }
}

// Switch to new mode, optionally restoring saved settings
// If pattern/brightness are -1, use saved values for that mode
static void switchToMode(char newMode, int pattern, int brightness) {
    // Save current mode settings before switching
    saveCurrentModeSettings();

    state.mode = newMode;

    // Restore saved settings or use provided values
    switch (newMode) {
        case 'M':
            state.pattern = (pattern >= 0) ? pattern : savedMusic.pattern;
            state.brightness = (brightness >= 0) ? brightness : savedMusic.brightness;
            break;
        case 'S':
            state.pattern = (pattern >= 0) ? pattern : savedSolid.pattern;
            state.brightness = (brightness >= 0) ? brightness : savedSolid.brightness;
            break;
        case 'P':
            state.pattern = (pattern >= 0) ? pattern : savedPattern.pattern;
            state.brightness = (brightness >= 0) ? brightness : savedPattern.brightness;
            break;
        case '0':
            // Off mode - keep brightness for when we come back
            state.pattern = 0;
            // Don't change brightness so it's remembered
            break;
        default:
            state.pattern = (pattern >= 0) ? pattern : 0;
            state.brightness = (brightness >= 0) ? brightness : 100;
            break;
    }

    FastLED.setBrightness(state.brightness);
    serialDataPending = true;
}

// Binary: [SOF=0xAA][type][seq][len][payload][crc16][EOF=0xBB]
// ============================================================================
void processSerialData() {
    static unsigned long lastDebugTime = 0;
    static int packetsReceived = 0;
    static int commandsReceived = 0;
    static int crcErrors = 0;
    static unsigned long lastByteMs = 0;

    auto resyncFromBuffer = [&]() {
        // Try to recover if a SOF appears inside the current buffer.
        int sofIndex = -1;
        for (int i = 1; i < rxIndex; i++) {
            if (rxBuffer[i] == Proto::SOF) {
                sofIndex = i;
                break;
            }
        }
        if (sofIndex >= 0) {
            int remaining = rxIndex - sofIndex;
            memmove(rxBuffer, rxBuffer + sofIndex, remaining);
            rxIndex = remaining;
        } else {
            rxIndex = 0;
        }
    };

    unsigned long nowMs = millis();
    if (rxIndex > 0 && (nowMs - lastByteMs) > 10) {
        // Stale partial frame, drop it to resync quickly.
        rxIndex = 0;
    }

    while (Serial1.available()) {
        uint8_t b = Serial1.read();
        lastByteMs = millis();

        if (rxIndex == 0) {
            // First byte - determine protocol (binary-only)
            if (b == Proto::SOF) {
                rxBuffer[rxIndex++] = b;
            }
            // else discard unknown byte
        } else {
            rxBuffer[rxIndex++] = b;

            // Have we read enough to know the length?
            // Frame: [SOF][type][seq][len][payload...][crc16][EOF]
            // Minimum header is 4 bytes before payload
            if (rxIndex >= 4) {
                uint8_t payloadLen = rxBuffer[3];
                size_t expectedFrameSize = 4 + payloadLen + 3;  // header + payload + crc16 + EOF

                uint8_t frameType = rxBuffer[1];
                bool lenOk = false;
                if (frameType == Proto::TYPE_FFT || frameType == Proto::TYPE_CMD) {
                    lenOk = (payloadLen == Proto::FIXED_PAYLOAD_LEN);
                } else if (frameType == Proto::TYPE_AUX) {
                    lenOk = (payloadLen == Proto::AUX_PAYLOAD_LEN);
                }

                if (expectedFrameSize > Proto::MAX_FRAME_SIZE || !lenOk) {
                    // Invalid length or type, reset
                    DBG_SERIAL_PRINTF("[RX DROP] type=0x%02X len=%u crc=not_checked\n", frameType, payloadLen);
                    resyncFromBuffer();
                    continue;
                }

                if ((size_t)rxIndex >= expectedFrameSize) {
                    // Complete frame received
                    // uint8_t frameSeq = rxBuffer[2];  // Available if needed
                    uint8_t* payload = &rxBuffer[4];

                    // Verify EOF
                    if (rxBuffer[expectedFrameSize - 1] != Proto::EOF_BYTE) {
                        uint16_t calcCrc = Proto::crc16_ccitt(&rxBuffer[1], 3 + payloadLen);
                        DBG_SERIAL_PRINTF("[RX DROP] len=%u crc=0x%04X\n", payloadLen, calcCrc);
                        resyncFromBuffer();
                        continue;
                    }

                    // Verify CRC (covers type, seq, len, payload)
                    uint16_t rxCrc = rxBuffer[4 + payloadLen] | (rxBuffer[5 + payloadLen] << 8);
                    uint16_t calcCrc = Proto::crc16_ccitt(&rxBuffer[1], 3 + payloadLen);

                    if (rxCrc != calcCrc) {
                        crcErrors++;
                        DBG_SERIAL_PRINTF("[RX DROP] len=%u crc=fail (rx=0x%04X calc=0x%04X)\n", payloadLen, rxCrc, calcCrc);
                        resyncFromBuffer();
                        continue;
                    }

                    // Process based on type
                    if (frameType == Proto::TYPE_FFT && payloadLen == Proto::FIXED_PAYLOAD_LEN) {
                        // FFT payload layout (68 bytes):
                        // 0-47:  12 floats (band amplitudes)
                        // 48-51: vocal bytes: env, syllableHit, note, noteStrength
                        // 52:    uint8 (spdif lock)
                        // 53-64: uint8[12] (chroma pitch class energies)
                        // 65:    uint8 (dominantPitch)
                        // 66:    uint8 (pitchStrength)
                        // 67:    uint8 (reserved)
                        currentBandCount = BANDS_12;
                        size_t floatBytes = static_cast<size_t>(BANDS_12) * sizeof(float);
                        memcpy(bandAmplitude, payload, floatBytes);
                        vocalEnv = payload[48];
                        vocalSyllable = payload[49];
                        vocalNote = payload[50];
                        vocalNoteStrength = payload[51];
                        beatAmplitude = 0.0f;
                        spdifLock = payload[52];
                        memcpy(chroma, &payload[53], 12);
                        dominantPitch = payload[65];
                        pitchStrength = payload[66];
                        vocalSustain = payload[67];

                        lastPacketTime = millis();
                        packetsReceived++;

                        // Debug: Print received values more frequently
                        static unsigned long lastRxDebug = 0;
                        if (millis() - lastRxDebug > 500) {
                            lastRxDebug = millis();
                        }
                    } else if (frameType == Proto::TYPE_CMD && payloadLen == Proto::FIXED_PAYLOAD_LEN) {
                        applyCommandPayload(payload);
                        commandsReceived++;
                    } else if (frameType == Proto::TYPE_AUX && payloadLen == Proto::AUX_PAYLOAD_LEN) {
                        // AUX payload layout (36 bytes):
                        // 0-11:  bandVis[12] (0-255)
                        // 12-23: bandDelta[12] (0-255)
                        // 24:    globalVis
                        // 25:    bassVis
                        // 26:    midVis
                        // 27:    trebleVis
                        // 28-29: majorPeakHz (uint16, Hz)
                        // 30:    majorPeakMag
                        // 31:    spectralFlux
                        // 32:    peakDetected
                        // 33:    activeBands
                        memcpy(bandVis8, &payload[0], BANDS_12);
                        memcpy(bandDelta8, &payload[12], BANDS_12);
                        globalVis8 = payload[24];
                        bassVis8 = payload[25];
                        midVis8 = payload[26];
                        trebleVis8 = payload[27];
                        majorPeakHz = (uint16_t)payload[28] | ((uint16_t)payload[29] << 8);
                        majorPeakMag = payload[30];
                        spectralFlux8 = payload[31];
                        peakDetected = payload[32];
                        lastAuxPacketMs = millis();
                    } else {
                        DBG_SERIAL_PRINTF("[RX DROP] len=%u crc=ok (type=0x%02X)\n", payloadLen, frameType);
                    }

                    rxIndex = 0;  // Reset for next frame
                }
            }
        }
    }

    // Debug output every second
    if (millis() - lastDebugTime > 3000) {
        if (packetsReceived > 0 || commandsReceived > 0 || crcErrors > 0) {
            DBG_SERIAL_PRINTF("RLED Serial1: %d FFT, %d CMD, %d CRC errors\n", packetsReceived, commandsReceived, crcErrors);
        }
        packetsReceived = 0;
        commandsReceived = 0;
        crcErrors = 0;
        lastDebugTime = millis();
    }

    // Warn if no FFT packets
    static unsigned long lastNoPacketWarning = 0;
    if (millis() - lastPacketTime > 2000 && millis() - lastNoPacketWarning > 5000) {
        DBG_SERIAL_PRINTLN("WARNING: No FFT packets from FFT Teensy");
        lastNoPacketWarning = millis();
    }
}

// ============================================================================
// USB Serial (Serial) Commands - ASCII CSV
// Format:
//   "0,0,0"             -> Off
//   "S,0,10"            -> Solid, pattern 0, brightness 10
//   "M,1,0,40"          -> Music, pattern 1, (colorIndex unused), brightness 40
// ============================================================================
void processUsbSerialCommands() {
    static char line[64];
    static size_t idx = 0;
    static int commaCount = 0;
    static unsigned long lastByteMs = 0;

    while (Serial.available()) {
        char c = (char)Serial.read();
        lastByteMs = millis();

        if (c == '\n' || c == '\r') {
            if (idx == 0) {
                continue;  // ignore empty lines
            }
            line[idx] = '\0';
            idx = 0;
            commaCount = 0;

            parseUsbCommandLine(line);
            continue;
        }

        if (idx < sizeof(line) - 1) {
            line[idx++] = c;
            if (c == ',') {
                commaCount++;
            }
        } else {
            // Line too long; reset buffer to avoid overflow
            idx = 0;
            commaCount = 0;
            DBG_SERIAL_PRINTLN("[USB CMD] Line too long");
        }
    }

    // If no newline is sent, parse after a short idle gap once we have 2 commas
    if (idx > 0 && commaCount >= 2 && (millis() - lastByteMs) > 50) {
        line[idx] = '\0';
        idx = 0;
        commaCount = 0;
        parseUsbCommandLine(line);
    }
}

// ============================================================================
// Initialize Virtual LED Strips (from main.cpp)
// ============================================================================
void initializeVirtualStrips() {
    // Each physical strip has 288 LEDs in zigzag: 0-143 UP, 144-287 DOWN
    // We remap so all virtual strips appear to go UP
    // Physical strips are right-to-left (pin 6 is rightmost = strip 0)
    // We reverse order so virtual strip 0 is leftmost (physical strip 5)

    for (int physStrip = 0; physStrip < NUM_APA102_STRIPS; physStrip++) {
        int baseIndex = physStrip * LEDS_PER_PHYSICAL_STRIP;

        // Reverse strip order: physical 5 -> virtual 0,1; physical 0 -> virtual 10,11
        int virtualBase = (NUM_APA102_STRIPS - 1 - physStrip) * 2;

        // Second half (LEDs 144-287) - going DOWN, reverse to appear UP - this is LEFT side
        for (int i = 0; i < LEDS_PER_VIRTUAL_STRIP; i++) {
            virtualLeds[virtualBase][i] = &leds[baseIndex + LEDS_PER_PHYSICAL_STRIP - 1 - i];
        }

        // First half (LEDs 0-143) - already going UP - this is RIGHT side
        for (int i = 0; i < LEDS_PER_VIRTUAL_STRIP; i++) {
            virtualLeds[virtualBase + 1][i] = &leds[baseIndex + i];
        }
    }

    DBG_SERIAL_PRINTLN("Virtual strips initialized (all going UP, left to right).");
}

// ============================================================================
// Display solid color on all LEDs
// ============================================================================
void displaySolidColor(CRGB color) {
    for (int strip = 0; strip < NUM_VIRTUAL_STRIPS; strip++) {
        for (int led = 0; led < LEDS_PER_VIRTUAL_STRIP; led++) {
            *virtualLeds[strip][led] = color;
        }
    }
    FastLED.show();
}

// ============================================================================
// Strip Test - fast rainbow snake through all LEDs
// ============================================================================
void stripTest() {
    DBG_SERIAL_PRINTLN("Running snake test (1728 LEDs)...");
    unsigned long startTime = millis();

    // Use global STRIP_PINS array from Globals.h
    int currentStrip = -1;

    const int SNAKE_LEN = 2;
    fill_solid(leds, 1728, CRGB::Black);

    for (int pos = 0; pos < 1728 + SNAKE_LEN; pos++) {
        // Print pin when entering a new strip
        if (pos < 1728) {
            int strip = pos / 288;
            if (strip != currentStrip) {
                currentStrip = strip;
                DBG_SERIAL_PRINTF("Strip %d - Pin %d (LEDs %d-%d)\n", strip, STRIP_PINS[strip], strip * 288, (strip + 1) * 288 - 1);
            }
        }

        // Turn off tail
        int tailPos = pos - SNAKE_LEN;
        if (tailPos >= 0 && tailPos < 1728) {
            leds[tailPos] = CRGB::Black;
        }
        // Light head
        if (pos < 1728) {
            uint8_t hue = (pos / 3) % 256;
            leds[pos] = CHSV(hue, 255, 255);
        }
        FastLED.show();
    }

    unsigned long elapsed = millis() - startTime;
    fill_solid(leds, 1728, CRGB::Black);
    FastLED.show();
    DBG_SERIAL_PRINTF("Snake test complete: %lu ms (%d FPS)\n", elapsed, (int)(1728000UL / elapsed));
}

// ============================================================================
// Virtual Strip Test - snake through virtual strips left to right, all going UP
// ============================================================================
void virtualStripTest() {
    DBG_SERIAL_PRINTLN("Running virtual strip test (left to right, all UP)...");
    fill_solid(leds, 1728, CRGB::Black);
    FastLED.show();

    // Snake through each virtual strip from left (0) to right (11)
    for (int vstrip = 0; vstrip < NUM_VIRTUAL_STRIPS; vstrip++) {
        DBG_SERIAL_PRINTF("Virtual Strip %d\n", vstrip);

        // Go UP the strip (0 to 143)
        for (int y = 0; y < LEDS_PER_VIRTUAL_STRIP; y++) {
            // Turn off previous LED
            if (y > 0) {
                *virtualLeds[vstrip][y - 1] = CRGB::Black;
            }
            // Light current LED with color based on strip
            uint8_t hue = (vstrip * 20) % 256;
            *virtualLeds[vstrip][y] = CHSV(hue, 255, 255);
            FastLED.show();
            delay(2);  // Small delay to see movement
        }
        // Turn off last LED before moving to next strip
        *virtualLeds[vstrip][LEDS_PER_VIRTUAL_STRIP - 1] = CRGB::Black;
    }

    fill_solid(leds, 1728, CRGB::Black);
    FastLED.show();
    DBG_SERIAL_PRINTLN("Virtual strip test complete.");
}

// ============================================================================
// Mode Handler
// ============================================================================
void handleLEDModes() {
    if (state.mode == '0') {
        displaySolidColor(CRGB::Black);
    } else if (state.mode == 'S') {
        if (state.pattern >= 0 && state.pattern < COLOR_OPTIONS_COUNT) {
            displaySolidColor(colorOptions[state.pattern]);
        } else {
            displaySolidColor(CRGB::Black);
        }
    } else if (state.mode == 'P') {
        runPattern();
    } else if (state.mode == 'M') {
        // Music visualization - same loop pattern as patterns for interruptability
        for (;;) {
            processUsbSerialCommands();
            processSerialData();
            if (serialDataPending) {
                break;
            }
            unsigned long now = millis();
            if (now - lastFrameTime >= FRAME_PERIOD_MS) {
                lastFrameTime = now;
                mapAmplitudesToLEDs();
            }
        }
    } else if (state.mode == 'A') {
        showArt();
    }
}

// ============================================================================
// Setup
// ============================================================================
void setup() {
    Serial.begin(115200);
    uint32_t serialWaitStart = millis();
    while (!Serial && (millis() - serialWaitStart) < 1500) {
        delay(10);
    }
    Serial1.begin(BAUD_RATE);
    delay(1000);

    loadFireSettings();
    
    DBG_SERIAL_PRINTLN("=== RaveGPT Teensy B: LED Display (SERIAL FFT MODE) ===");
    DBG_SERIAL_PRINT("Serial#: ");
    DBG_SERIAL_PRINTLN(usb_string_serial_number);
    DBG_SERIAL_PRINT("Compiled: ");
    DBG_SERIAL_PRINTLN(__FILE__);
    DBG_SERIAL_PRINTLN("Reading FFT data from Serial1");
    DBG_SERIAL_PRINTLN("Starting in Music visualization mode with live audio data\n");

    // Enable 74HCT245 buffer so RJ45 sees the signal
    pinMode(BUFFER_ENABLE, OUTPUT);
    digitalWrite(BUFFER_ENABLE, HIGH);

    // Initialize LED strips - data pins: 6,7,8,2,21,5 with shared clock on pin 14
    FastLED.addLeds<APA102, 6,  CLOCK_PIN, BGR, DATA_RATE_MHZ(FASTLED_SPI_SPEED)>(leds, 288).setCorrection(TypicalLEDStrip);
    FastLED.addLeds<APA102, 7,  CLOCK_PIN, BGR, DATA_RATE_MHZ(FASTLED_SPI_SPEED)>(leds + 288, 288).setCorrection(TypicalLEDStrip);
    FastLED.addLeds<APA102, 2,  CLOCK_PIN, BGR, DATA_RATE_MHZ(FASTLED_SPI_SPEED)>(leds + 576, 288).setCorrection(TypicalLEDStrip);
    FastLED.addLeds<APA102, 21,  CLOCK_PIN, BGR, DATA_RATE_MHZ(FASTLED_SPI_SPEED)>(leds + 864, 288).setCorrection(TypicalLEDStrip);
    FastLED.addLeds<APA102, 5, CLOCK_PIN, BGR, DATA_RATE_MHZ(FASTLED_SPI_SPEED)>(leds + 1152, 288).setCorrection(TypicalLEDStrip);
    FastLED.addLeds<APA102, 8,  CLOCK_PIN, BGR, DATA_RATE_MHZ(FASTLED_SPI_SPEED)>(leds + 1440, 288).setCorrection(TypicalLEDStrip);
    
    // Power limiting - 5V @ 50A
    FastLED.setMaxPowerInMilliWatts(250000);  // 5V * 50A = 250W = 250000mW
    FastLED.setBrightness(10);
    
    initializeVirtualStrips();

    DBG_SERIAL_PRINTLN("FastLED initialized.");

#ifdef DEBUG_LED_TEST
    DBG_SERIAL_PRINTLN("Running LED tests...");
    stripTest();
    virtualStripTest();
    DBG_SERIAL_PRINTLN("LED tests complete.");
#endif

    DBG_SERIAL_PRINTLN("Waiting for FFT frames from master...");
}

// ============================================================================
// Main Loop
// ============================================================================
void loop() {
    unsigned long now = millis();

    // Read USB serial commands (ASCII CSV)
    processUsbSerialCommands();

    // Read FFT packets from Serial1
    processSerialData();

    // Render at ~60 FPS
    if (now - lastFrameTime >= FRAME_PERIOD_MS) {
        lastFrameTime = now;
        frameCount++;
        
        // Run current mode
        #if DEBUG_STATUS
        uint32_t workStart = micros();
        #endif
        handleLEDModes();
        #if DEBUG_STATUS
        uint32_t workUs = micros() - workStart;
        frameWorkAccumUs += workUs;
        if (workUs > frameWorkMaxUs) {
            frameWorkMaxUs = workUs;
        }
        #endif
        serialDataPending = false;  // Clear after mode runs
        
        // Status printing disabled (CPU printed on command only)
    }

}

// ============================================================================
// End of Teensy B: LED Display Slave
// ============================================================================
