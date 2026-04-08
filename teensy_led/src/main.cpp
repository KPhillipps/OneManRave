#include <Arduino.h>
extern "C" char usb_string_serial_number[];
#include "Globals.h"

// Uncomment to run LED strip tests on startup
// #define DEBUG_LED_TEST

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
CRGB leds[1728];    // Physical array: 6 strips × 288 LEDs
CRGB* virtualLeds[NUM_VIRTUAL_STRIPS][LEDS_PER_VIRTUAL_STRIP];  // 12 virtual strips, each LED mapped individually
constexpr int LEDL = NUM_STRIPS * LEDS_PER_PHYSICAL_STRIP;
#ifdef USE_WS2812B
static constexpr uint32_t kMaxLedPowerMilliWatts = 258000;  // 6 WS2812B strips × 43W
#else
static constexpr uint32_t kMaxLedPowerMilliWatts = 300000;  // 6 APA102 strips
#endif
static constexpr uint8_t kBrightnessStepMax = 31;

static constexpr uint8_t mapBrightnessInputToRaw(int brightness) {
    return (uint8_t)(((brightness < 0 ? 0 : (brightness > (int)kBrightnessStepMax ? (int)kBrightnessStepMax : brightness)) * 255
        + (kBrightnessStepMax / 2)) / kBrightnessStepMax);
}

// State (mode/pattern/brightness)
// Default: Off mode on boot
State state = {'0', 0, mapBrightnessInputToRaw(10)};

// Stored settings per mode - restored when switching modes
struct ModeSettings {
    int pattern;      // Pattern/color index
    int brightness;   // Brightness level
};
ModeSettings savedMusic = {0, mapBrightnessInputToRaw(10)};    // Music mode: visualization 0
ModeSettings savedSolid = {0, mapBrightnessInputToRaw(10)};    // Solid mode: color 0
ModeSettings savedPattern = {0, mapBrightnessInputToRaw(10)};  // Pattern mode: pattern 0 (rainbow)

bool firstRun = false;
bool message = true;
volatile bool serialDataPending = false;  // Set TRUE when a command is parsed, patterns check this

// Serial port:
//  - All frames (FFT/AUX/CMD) from FFT Teensy on Serial1 (RX1 = pin 0)
//    Commands originate from ESP32, forwarded through FFT Teensy
HardwareSerial& SERIAL_FFT = Serial1;

// ============================================================================
// Serial Receive (Framed Protocol with CRC)
// ============================================================================
const uint32_t BAUD_RATE = 460800;  // Must match FFT Teensy output (and ESP32 for CMD)

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
const unsigned long FRAME_PERIOD_MS = 17;  // ~60 FPS
unsigned long lastPacketTime = 0;
unsigned long cpuWindowStartMs = 0;
unsigned long cpuFrameCount = 0;
uint64_t cpuWorkAccumUs = 0;
unsigned long cpuWorkMaxUs = 0;

static const char* patternLabelForMode(char mode) {
    switch (mode) {
        case 'S': return "Color";
        case 'M': return "Viz";
        case 'P': return "Pattern";
        default:  return "Pattern";
    }
}

static uint8_t effectiveBrightnessForCurrentFrame() {
    return calculate_max_brightness_for_power_mW(FastLED.getBrightness(), kMaxLedPowerMilliWatts);
}

static void recordFrameWork(unsigned long nowMs, unsigned long workUs) {
    if (cpuWindowStartMs == 0) {
        cpuWindowStartMs = nowMs;
    }
    cpuFrameCount++;
    cpuWorkAccumUs += workUs;
    if (workUs > cpuWorkMaxUs) {
        cpuWorkMaxUs = workUs;
    }
}

static void printStatusOnCommand(unsigned long nowMs) {
    if (!Serial) {
        return;
    }

    unsigned long windowMs = (cpuWindowStartMs != 0) ? (nowMs - cpuWindowStartMs) : 0;
    float fps = (windowMs > 0 && cpuFrameCount > 0)
        ? (cpuFrameCount * 1000.0f) / windowMs
        : 0.0f;
    float avgWorkUs = (cpuFrameCount > 0)
        ? ((float)cpuWorkAccumUs / (float)cpuFrameCount)
        : 0.0f;
    float avgCpuPct = (avgWorkUs / (FRAME_PERIOD_MS * 1000.0f)) * 100.0f;
    float peakCpuPct = ((float)cpuWorkMaxUs / (FRAME_PERIOD_MS * 1000.0f)) * 100.0f;
    unsigned long auxAgeMs = (lastAuxPacketMs != 0)
        ? (nowMs - lastAuxPacketMs)
        : 0;

    Serial.printf("[STAT] FPS:%.1f | Mode:%c | %s:%d | Brt:%u | CPU avg:%.2f%% peak:%.2f%%",
                  fps, state.mode, patternLabelForMode(state.mode), state.pattern, effectiveBrightnessForCurrentFrame(),
                  avgCpuPct, peakCpuPct);
    if (state.mode == 'M') {
        Serial.printf(" | SPDIF:%u | AuxAge:%lums | Bands:", spdifLock, auxAgeMs);
        int bands = currentBandCount;
        if (bands < 1) bands = 1;
        if (bands > MAX_BANDS) bands = MAX_BANDS;
        for (int i = 0; i < bands; i++) {
            Serial.print(' ');
            Serial.print(bandAmplitude[i], 4);
        }
    }
    Serial.println();

    cpuWindowStartMs = nowMs;
    cpuFrameCount = 0;
    cpuWorkAccumUs = 0;
    cpuWorkMaxUs = 0;
}

// ============================================================================
// Serial Receiver - FFT/AUX/CMD all arrive on Serial1
// ============================================================================
// Forward declaration (used by applyCommandPayload)
static void switchToMode(char newMode, int pattern, int brightness);
static void applyCommandPayload(const uint8_t* payload);

// Binary framing: [SOF=0xAA][type][seq][len][payload][crc16][EOF=0xBB]
struct RxState {
    uint8_t buffer[Proto::MAX_FRAME_SIZE];
    size_t  index = 0;
    unsigned long lastByteMs = 0;
};

static RxState rxFFT;

static void parseSerialFrames(HardwareSerial& port, RxState& st, bool allowFFT, bool allowAUX, bool allowCMD) {
    auto resyncFromBuffer = [&]() {
        if (st.index > 0) {
            uint8_t lastBytes[2] = { st.buffer[st.index - 2], st.buffer[st.index - 1] };
            st.buffer[0] = lastBytes[0];
            st.buffer[1] = lastBytes[1];
            st.index = 0;
        }
    };

    if (st.index > 0 && (millis() - st.lastByteMs) > 10) {
        resyncFromBuffer();
    }

    while (port.available()) {
        uint8_t b = port.read();
        st.lastByteMs = millis();

        if (st.index == 0) {
            if (b == Proto::SOF) st.buffer[st.index++] = b;
        } else {
            st.buffer[st.index++] = b;

            if (st.index >= 4) {
                uint8_t payloadLen = st.buffer[3];
                size_t expectedFrameSize = 4 + payloadLen + 3;

                uint8_t frameType = st.buffer[1];
                bool lenOk = false;
                if (frameType == Proto::TYPE_FFT || frameType == Proto::TYPE_CMD) {
                    lenOk = (payloadLen == Proto::FIXED_PAYLOAD_LEN);
                } else if (frameType == Proto::TYPE_AUX) {
                    lenOk = (payloadLen == Proto::AUX_PAYLOAD_LEN);
                }

                if (expectedFrameSize > Proto::MAX_FRAME_SIZE || !lenOk) {
                    DBG_SERIAL_PRINTF("[RX DROP] type=0x%02X len=%u crc=not_checked\n", frameType, payloadLen);
                    resyncFromBuffer();
                    continue;
                }

                if (st.index >= expectedFrameSize) {
                    uint8_t* payload = &st.buffer[4];

                    if (st.buffer[expectedFrameSize - 1] != Proto::EOF_BYTE) {
                        uint16_t calcCrc = Proto::crc16_ccitt(&st.buffer[1], 3 + payloadLen);
                        (void)calcCrc;
                        DBG_SERIAL_PRINTF("[RX DROP] len=%u crc=0x%04X\n", payloadLen, calcCrc);
                        resyncFromBuffer();
                        continue;
                    }

                    uint16_t rxCrc = st.buffer[4 + payloadLen] | (st.buffer[5 + payloadLen] << 8);
                    uint16_t calcCrc = Proto::crc16_ccitt(&st.buffer[1], 3 + payloadLen);
                    (void)calcCrc;
                    if (rxCrc != calcCrc) {
                        DBG_SERIAL_PRINTF("[RX DROP] len=%u crc=fail (rx=0x%04X calc=0x%04X)\n", payloadLen, rxCrc, calcCrc);
                        resyncFromBuffer();
                        continue;
                    }

                    if (frameType == Proto::TYPE_FFT && allowFFT && payloadLen == Proto::FIXED_PAYLOAD_LEN) {
                        currentBandCount = BANDS_12;
                        memcpy(bandAmplitude, payload, BANDS_12 * sizeof(float));
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
                    } else if (frameType == Proto::TYPE_CMD && allowCMD && payloadLen == Proto::FIXED_PAYLOAD_LEN) {
                        applyCommandPayload(payload);
                    } else if (frameType == Proto::TYPE_AUX && allowAUX && payloadLen == Proto::AUX_PAYLOAD_LEN) {
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
                    }

                    st.index = 0;
                }
            }
        }
    }
}

// Legacy entry used by patterns
void processSerialData() {
    parseSerialFrames(SERIAL_FFT, rxFFT, true, true, true);   // FFT/AUX/CMD from FFT Teensy
}

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
    Serial.printf("[CMD] mode=%c pattern=%d brightness=%u\n", state.mode, state.pattern, effectiveBrightnessForCurrentFrame());
    printStatusOnCommand(millis());
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
            state.brightness = (brightness >= 0) ? mapBrightnessInputToRaw(brightness) : savedMusic.brightness;
            break;
        case 'S':
            state.pattern = (pattern >= 0) ? pattern : savedSolid.pattern;
            state.brightness = (brightness >= 0) ? mapBrightnessInputToRaw(brightness) : savedSolid.brightness;
            break;
        case 'P':
            state.pattern = (pattern >= 0) ? pattern : savedPattern.pattern;
            state.brightness = (brightness >= 0) ? mapBrightnessInputToRaw(brightness) : savedPattern.brightness;
            break;
        case '0':
            // Off mode - keep brightness for when we come back
            state.pattern = 0;
            // Don't change brightness so it's remembered
            break;
        default:
            state.mode = '0';
            state.pattern = 0;
            break;
    }

    FastLED.setBrightness(state.brightness);
    serialDataPending = true;
}

// (legacy parser removed; using parseSerialFrames instead)

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
// ----------------------------------------------------------------------------
// Virtual strip mapping
// ----------------------------------------------------------------------------
// virtualLeds[v][0..143] is virtual strip v, indexed bottom -> top.
// Virtual strips run left -> right across the installation: v=0 is leftmost,
// v=11 is rightmost. Each physical 288-LED chain holds two halves; one half
// has data entering at the bottom ("up" half: chain LEDs 0..143 already go
// physical-up) and the other has data entering at the top ("down" half:
// chain LEDs 144..287 must be reversed to appear physical-up).
//
// The two LED installations have DIFFERENT physical layouts:
//   - APA102 right side: pin order matches STRIP_PINS = {6,7,2,21,5,8} but
//     physical order is right-to-left (pin 6 rightmost) and every pair has
//     the down-half on the left and the up-half on the right.
//   - WS2812B left side: physical L->R order is pin7, pin6, pin21, pin2,
//     pin8, pin14. Up/down side varies per pair (verified 2026-04-07 by
//     strip_id_test). Pin 5 is NOT wired on this board; pin 14 replaces it.
// ----------------------------------------------------------------------------

// Map a (chainIndex, halfStartLed, isUpHalf) tuple into virtualLeds[v].
// chainIndex is the FastLED.addLeds order: 0..5 within the 1728-LED `leds`
// buffer.
static void mapVirtualStrip(int v, int chainIndex, bool isUpHalf) {
    int baseIndex = chainIndex * LEDS_PER_PHYSICAL_STRIP;
    if (isUpHalf) {
        // chain LEDs 0..143 already go bottom -> top
        for (int i = 0; i < LEDS_PER_VIRTUAL_STRIP; i++) {
            virtualLeds[v][i] = &leds[baseIndex + i];
        }
    } else {
        // chain LEDs 144..287 enter at top, reverse so v[0] = bottom
        for (int i = 0; i < LEDS_PER_VIRTUAL_STRIP; i++) {
            virtualLeds[v][i] = &leds[baseIndex + LEDS_PER_PHYSICAL_STRIP - 1 - i];
        }
    }
}

void initializeVirtualStripsAPA102Right() {
    // Each physical strip has 288 LEDs in zigzag: 0-143 UP, 144-287 DOWN.
    // Physical strips are right-to-left (pin 6 is rightmost = chain 0).
    // Reverse order so virtual strip 0 is leftmost (chain 5).
    for (int chain = 0; chain < NUM_STRIPS; chain++) {
        int virtualBase = (NUM_STRIPS - 1 - chain) * 2;
        mapVirtualStrip(virtualBase + 0, chain, false);  // down-half = LEFT of pair
        mapVirtualStrip(virtualBase + 1, chain, true);   // up-half   = RIGHT of pair
    }
    DBG_SERIAL_PRINTLN("Virtual strips initialized: APA102 right (12 strips L->R).");
}

void initializeVirtualStripsWS2812BLeft() {
    // Verified physical L->R layout for the WS2812B (left) installation.
    // chainIndex follows the FastLED.addLeds order in the WS2812B branch:
    //   chain 0 = pin 6, chain 1 = pin 7, chain 2 = pin 2,
    //   chain 3 = pin 21, chain 4 = pin 14, chain 5 = pin 8
    // Pairs:
    //   v 0: pin7  up    v 1: pin7  down
    //   v 2: pin6  down  v 3: pin6  up
    //   v 4: pin21 down  v 5: pin21 up
    //   v 6: pin2  up    v 7: pin2  down
    //   v 8: pin8  up    v 9: pin8  down
    //   v10: pin14 down  v11: pin14 up
    mapVirtualStrip( 0, /*pin7  chain*/ 1, true  );
    mapVirtualStrip( 1, /*pin7  chain*/ 1, false );
    mapVirtualStrip( 2, /*pin6  chain*/ 0, false );
    mapVirtualStrip( 3, /*pin6  chain*/ 0, true  );
    mapVirtualStrip( 4, /*pin21 chain*/ 3, false );
    mapVirtualStrip( 5, /*pin21 chain*/ 3, true  );
    mapVirtualStrip( 6, /*pin2  chain*/ 2, true  );
    mapVirtualStrip( 7, /*pin2  chain*/ 2, false );
    mapVirtualStrip( 8, /*pin8  chain*/ 5, true  );
    mapVirtualStrip( 9, /*pin8  chain*/ 5, false );
    mapVirtualStrip(10, /*pin14 chain*/ 4, false );
    mapVirtualStrip(11, /*pin14 chain*/ 4, true  );
    DBG_SERIAL_PRINTLN("Virtual strips initialized: WS2812B left (12 strips L->R).");
}

// Legacy entry point - defaults to the APA102 right layout, since that's the
// original wiring this project was built for. Boards that need the WS2812B
// layout call initializeVirtualStripsWS2812BLeft() directly.
void initializeVirtualStrips() {
    initializeVirtualStripsAPA102Right();
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
            parseSerialFrames(SERIAL_FFT, rxFFT, true, true, true);   // FFT/AUX/CMD from FFT Teensy
            if (serialDataPending) {
                break;
            }
            unsigned long now = millis();
            if (now - lastFrameTime >= FRAME_PERIOD_MS) {
                unsigned long workStart = micros();
                lastFrameTime = now;
                mapAmplitudesToLEDs();
                unsigned long workUs = micros() - workStart;
                recordFrameWork(now, workUs);
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
    SERIAL_FFT.begin(BAUD_RATE);  // All frames (FFT/AUX/CMD) from FFT Teensy on Serial1 (RX1/pin 0)
    delay(1000);

    loadFireSettings();
    
    DBG_SERIAL_PRINTLN("=== RaveGPT Teensy B: LED Display (SERIAL FFT MODE) ===");

    // ------------------------------------------------------------------
    // Auto-detect which physical LED Teensy this is based on chip serial.
    // The PJRC-assigned USB serial number is derived from HW_OCOTP_MAC0,
    // which we can read directly at runtime as a uint32_t.
    //
    // Get a board's serial via Mac terminal:
    //   system_profiler SPUSBDataType | grep -A 10 -i teensy
    //
    // Known boards:
    //   19169950  -> LEFT  side  (WS2812B)
    //   <TBD>     -> RIGHT side  (APA102)   [placeholder]
    // ------------------------------------------------------------------
    constexpr uint32_t SERIAL_LEFT_WS2812B  = 19169950;
    constexpr uint32_t SERIAL_RIGHT_APA102  = 0xFFFFFFFF;  // TODO: fill in

    const uint32_t boardSerial = HW_OCOTP_MAC0;
    // FORCED: only WS2812B left board exists right now. HW_OCOTP_MAC0 does not
    // equal the PJRC-derived USB serial (19169950), so the auto-detect was
    // falling through to the APA102 branch and driving WS2812B strips with SPI
    // = garbage noise even on solid-white fills. Hardcode until the right
    // board is wired and we know both MAC0 values.
    const bool isLeftWS2812B   = true;
    const bool isRightAPA102   = false;
    (void)SERIAL_LEFT_WS2812B;
    (void)SERIAL_RIGHT_APA102;

    Serial.print("Board MAC0 (raw HW_OCOTP_MAC0): ");
    Serial.println(boardSerial);
    Serial.println("Forced branch: LEFT (WS2812B)");

    DBG_SERIAL_PRINT("Compiled: ");
    DBG_SERIAL_PRINTLN(__FILE__);
    DBG_SERIAL_PRINTLN("Reading FFT data from Serial1 (RX1/pin 0)");
    DBG_SERIAL_PRINTLN("Starting in Off mode; waiting for commands\n");

    // Initialize LED strips - branch on detected board.
    if (isLeftWS2812B) {
        // Left side: WS2812B (single data wire, GRB order). Pin 5 is NOT
        // wired on this board; pin 14 is the 6th data line (no clock needed).
        // Chain order: 0=pin6, 1=pin7, 2=pin2, 3=pin21, 4=pin14, 5=pin8.
        FastLED.addLeds<WS2812B, 6,  GRB>(leds,        288);
        FastLED.addLeds<WS2812B, 7,  GRB>(leds + 288,  288);
        FastLED.addLeds<WS2812B, 2,  GRB>(leds + 576,  288);
        FastLED.addLeds<WS2812B, 21, GRB>(leds + 864,  288);
        FastLED.addLeds<WS2812B, 14, GRB>(leds + 1152, 288);
        FastLED.addLeds<WS2812B, 8,  GRB>(leds + 1440, 288);
    } else {
        // Right side: APA102 (SPI with shared clock, BGR order). Default
        // for unknown boards too — APA102 is the original wiring.
        FastLED.addLeds<APA102, 6,  CLOCK_PIN, BGR, DATA_RATE_MHZ(10)>(leds, 288);
        FastLED.addLeds<APA102, 7,  CLOCK_PIN, BGR, DATA_RATE_MHZ(10)>(leds + 288, 288);
        FastLED.addLeds<APA102, 2,  CLOCK_PIN, BGR, DATA_RATE_MHZ(10)>(leds + 576, 288);
        FastLED.addLeds<APA102, 21, CLOCK_PIN, BGR, DATA_RATE_MHZ(10)>(leds + 864, 288);
        FastLED.addLeds<APA102, 5,  CLOCK_PIN, BGR, DATA_RATE_MHZ(10)>(leds + 1152, 288);
        FastLED.addLeds<APA102, 8,  CLOCK_PIN, BGR, DATA_RATE_MHZ(10)>(leds + 1440, 288);
    }

    // Power limiting
    FastLED.setMaxPowerInMilliWatts(kMaxLedPowerMilliWatts);
    FastLED.setBrightness(mapBrightnessInputToRaw(10));
    FastLED.clear(true);

    if (isLeftWS2812B) {
        initializeVirtualStripsWS2812BLeft();
    } else {
        initializeVirtualStripsAPA102Right();
    }

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

    // Read framed data (FFT/AUX/CMD all from FFT Teensy on Serial1)
    parseSerialFrames(SERIAL_FFT, rxFFT, true, true, true);

    // Render at ~60 FPS
    if (now - lastFrameTime >= FRAME_PERIOD_MS) {
        unsigned long workStart = micros();
        lastFrameTime = now;
        frameCount++;
        handleLEDModes();
        serialDataPending = false;  // Clear after mode runs
        if (state.mode != 'M') {
            unsigned long workUs = micros() - workStart;
            recordFrameWork(now, workUs);
        }
    }

}

// ============================================================================
// End of Teensy B: LED Display Slave
// ============================================================================
