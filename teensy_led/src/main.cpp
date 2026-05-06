#include <Arduino.h>
extern "C" char usb_string_serial_number[];
#include "Globals.h"

// Uncomment to run LED strip tests on startup
// #define DEBUG_LED_TEST

#ifdef USE_WS2812B
// FastLED's OCTOWS2811 controller wraps OctoWS2811 for DMA-driven parallel
// output. USE_OCTOWS2811 must be defined (build flag) and OctoWS2811.h must
// be included BEFORE FastLED.h.
#include <OctoWS2811.h>
#endif
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
// ========================== RX PROTOCOLS ==========================
// Serial1 (pin 0)  — CMD only, ASCII CSV from the ESP32 @ 460800 baud
//   Format: "mode,val1,val2,val3\r\n" (matches the USB command interface)
// Serial4 (pin 16) — FFT + AUX from the FFT Teensy @ 460800 baud, framed
//   Frame: [0xAA][type][seq][len][payload][crc16][0xBB]
//   - type=0x01: FFT payload (68 bytes): 12 floats + vocal bytes + spdif + chroma[12] + dominantPitch + pitchStrength + reserved
//   - type=0x02: CMD payload (68 bytes): mode, pattern, pattern, brightness, then padding (legacy / right-side path)
//   - type=0x03: AUX payload (36 bytes): bandVis/bandDelta + peak + flux + derived metrics
//
// ========================== MODES ==========================
//   '0' = Off (all black)
//   'S' = Solid color (uses pattern -> colorOptions[])
//   'P' = Pattern animation (pattern 2-10, see Patterns.cpp)
//   'M' = Music visualization (uses FFT data)
//   'A' = Art/bitmap display
// ============================================================================

// Global LED Arrays.
//   - WS2812B path: 8 OctoWS2811 channels × 288 = 2304 (ch5/ch7 unused but required)
//   - APA102 path:  6 strips × 288 = 1728
CRGB leds[LED_BUFFER_SIZE];
CRGB* virtualLeds[NUM_VIRTUAL_STRIPS][LEDS_PER_VIRTUAL_STRIP];  // 12 virtual strips, each LED mapped individually
constexpr int LEDL = NUM_STRIPS * LEDS_PER_PHYSICAL_STRIP;
#ifdef USE_WS2812B
// Power budget for FastLED's runtime power scaler. We already cap raw
// brightness in mapBrightnessInputToRaw() (kMaxRawBrightness = 230 ≈ 90%
// true ≈ 78A for all-white on the 80A supply), so this budget should be
// HIGHER than the hard cap allows — otherwise FastLED's calc would scale
// brightness down further on bright frames and the user would see the
// 90% setting deliver less than 90%. 400 W = full 80A supply rating;
// our hard cap keeps us under that, so this acts purely as a sanity net
// in case something pushes the buffer past expected current.
static constexpr uint32_t kMaxLedPowerMilliWatts = 400000;
#else
static constexpr uint32_t kMaxLedPowerMilliWatts = 300000;  // 6 APA102 strips
#endif
// Brightness input scale — the value sent over the wire from the ESP32.
// 31 steps gives ~4 seconds floor→ceiling when the Apple Remote's
// brightness +/- button is held (~7 IR repeats/sec) — fine enough to
// dial in, fast enough not to bore you. 5-bit fits cleanly into a byte.
static constexpr uint8_t kBrightnessStepMax = 31;

// Hard ceiling on the raw 0..255 value handed to FastLED. Calibrated for
// the 5V × 80A supply driving 1728 WS2812B LEDs:
//   1728 × ~60 mA full white = ~104 A unscaled (way over 80 A)
//   90% true brightness → 230/255 → ~78 A for all-white worst case,
//   leaving ~2 A safety margin to the supply rating.
static constexpr uint8_t kMaxRawBrightness = 230;

// Floor on raw output for any step ≥ 1. WS2812B with 8-bit PWM at raw=1
// or 2 is barely-distinguishable-from-off; 3 is the lowest reliably
// visible value. Step 1 should always feel clearly on.
static constexpr uint8_t kMinRawBrightness = 3;

// Map step (0..31) to raw 0..230 along a gamma-2.0 curve. Linear stepping
// would put 14 of the 31 steps in the 0..50 raw range (visually
// indistinguishable) and only 1 step every 7 raw values up high (also
// visually indistinguishable). Gamma curve gives perceptually equal jumps:
// step 1 vs 2 looks the same magnitude of change as step 30 vs 31.
//
// Formula: raw = kMinRawBrightness + (step² / 31²) × (cap − floor)
// Done in integer math, no FastLED gamma tables, no floating point.
//   step  0 → raw 0   (LEDs off)
//   step  1 → raw 3   (just-visible floor)
//   step  5 → raw 8
//   step 10 → raw 26
//   step 16 → raw 63   (~25% perceived)
//   step 24 → raw 139  (~55% perceived)
//   step 31 → raw 230  (90% perceived, hard supply cap)
static constexpr uint8_t mapBrightnessInputToRaw(int brightness) {
    if (brightness <= 0) return 0;
    if (brightness > (int)kBrightnessStepMax) brightness = kBrightnessStepMax;
    constexpr uint16_t kRange = (uint16_t)kMaxRawBrightness - (uint16_t)kMinRawBrightness;
    constexpr uint16_t kMaxSq = (uint16_t)kBrightnessStepMax * (uint16_t)kBrightnessStepMax;
    uint32_t curved = ((uint32_t)brightness * (uint32_t)brightness * (uint32_t)kRange) / (uint32_t)kMaxSq;
    uint16_t raw = (uint16_t)curved + (uint16_t)kMinRawBrightness;
    if (raw > kMaxRawBrightness) raw = kMaxRawBrightness;
    return (uint8_t)raw;
}

// State (mode/pattern/brightness)
// Default: Solid white at the lowest visible brightness step (1 = 1%).
// LEDs come on dim the moment setup() finishes, confirming the hardware
// path is live; the remote's brightness-up button steps from there.
State state = {'S', 0, mapBrightnessInputToRaw(1)};

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

// Two dedicated receive ports — one purpose each, no mixed traffic:
//   Serial1 (pin 0  RX) : CMD only, ASCII CSV from the ESP32 (TX pin GPIO 39)
//   Serial4 (pin 16 RX): FFT + AUX only, binary frames from the FFT Teensy
//                         (its Serial2 TX = pin 8). Yes, the README/HARDWARE.md
//                         claim FFT TX is pin 1 — those docs are stale; the
//                         actual FFT firmware uses Serial2.
HardwareSerial& SERIAL_CMD = Serial1;
HardwareSerial& SERIAL_FFT = Serial4;

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

// RX status counters. Ungated on purpose — this status line survives
// production builds so you can always tell at a glance whether bytes /
// frames are arriving on either RX pin. Counters sum across BOTH ports
// (Serial1 CMD path + Serial4 FFT path); the per-frame counters break
// down by type, so a missing FFT data feed shows as fft=0/aux=0 even
// when cmd>0. Printed once every 10s from loop(), then reset.
static uint32_t rxByteCount  = 0;  // raw bytes read off Serial1 + Serial4
static uint32_t rxFftCount   = 0;  // valid TYPE_FFT frames applied (Serial4)
static uint32_t rxAuxCount   = 0;  // valid TYPE_AUX frames applied (Serial4)
static uint32_t rxCmdCount   = 0;  // valid CMD frames applied (Serial1, ASCII or binary)
static uint32_t rxDropCount  = 0;  // frames dropped (bad length / EOF / CRC)
static unsigned long lastRxStatusMs = 0;
static const unsigned long RX_STATUS_PERIOD_MS = 10000;  // 10-second status

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
// Serial Receiver - parseSerialFrames() is called once per RX port per loop:
//   parseSerialFrames(SERIAL_CMD=Serial1, ..., allowFFT=false, allowAUX=false, allowCMD=true)
//   parseSerialFrames(SERIAL_FFT=Serial4, ..., allowFFT=true,  allowAUX=true,  allowCMD=false)
//
// Two on-the-wire protocols are accepted, disambiguated by the first byte:
//   (a) Binary framed  [0xAA][type][seq][len][payload][crc16][0xBB]
//       Used by the right-side FFT Teensy on Serial4 (FFT/AUX) and was
//       also used historically on Serial1 when the FFT was forwarding cmds.
//   (b) ASCII CSV      "mode,val1,val2,val3\r\n"
//       Used by the left-side ESP32 on Serial1, same text the USB command
//       interface accepts. 0xAA kicks off binary frame parsing; anything
//       else is accumulated as ASCII until newline, then handed to the
//       existing parseUsbCommandLine().
// ============================================================================
// Forward declarations
static void switchToMode(char newMode, int pattern, int brightness);
static void applyCommandPayload(const uint8_t* payload);
static void parseUsbCommandLine(const char* line);  // defined later; shared by USB and Serial1 ASCII paths

// Binary framing state
struct RxState {
    uint8_t buffer[Proto::MAX_FRAME_SIZE];
    size_t  index = 0;
    unsigned long lastByteMs = 0;
};

static RxState rxFFT;   // Serial4 — FFT + AUX from FFT Teensy
static RxState rxCmd;   // Serial1 — CMD from ESP32

// ASCII CSV line accumulator. Used by parseSerialFrames() when the input
// stream is text (ESP32 → Serial1 case). Both parseSerialFrames calls
// share this buffer, but only Serial1 is expected to deliver ASCII; if
// Serial4 ever produced printable noise it could interleave. Separate from
// the USB ASCII path so USB and hardware-UART partial lines can't clobber
// each other.
static char     rxAsciiLine[32];
static size_t   rxAsciiIdx     = 0;
static int      rxAsciiCommas  = 0;
static unsigned long rxAsciiLastByteMs = 0;

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

    // Idle-flush a stale ASCII line if nothing has arrived for a while (e.g.
    // sender dropped mid-line). Without this, noise could leave the buffer
    // permanently half-full.
    if (rxAsciiIdx > 0 && (millis() - rxAsciiLastByteMs) > 100) {
        rxAsciiIdx    = 0;
        rxAsciiCommas = 0;
    }

    while (port.available()) {
        uint8_t b = port.read();
        rxByteCount++;  // counted regardless of whether it makes it into a valid frame
        st.lastByteMs = millis();

        if (st.index == 0) {
            if (b == Proto::SOF) {
                // Start of a binary frame. Flush any ASCII line in progress
                // (would be corrupt if a binary frame interrupted it anyway).
                rxAsciiIdx    = 0;
                rxAsciiCommas = 0;
                st.buffer[st.index++] = b;
                continue;
            }
            // Not a binary SOF -> try ASCII CSV path.
            rxAsciiLastByteMs = millis();
            if (b == '\n' || b == '\r') {
                // Line terminator. If we have a plausible CSV (at least 2
                // commas, matching the "mode,val1,val2,val3" format the
                // ESP32 sends), parse it. Otherwise discard.
                if (rxAsciiIdx > 0 && rxAsciiCommas >= 2) {
                    rxAsciiLine[rxAsciiIdx] = '\0';
                    // Echo the full raw line so you can see exactly what arrived
                    // on the wire before it's parsed/validated. The [CMD] line
                    // that applyCommandPayload emits afterward shows how it was
                    // interpreted and applied.
                    Serial.printf("[RX CMD RAW] \"%s\"\n", rxAsciiLine);
                    parseUsbCommandLine(rxAsciiLine);
                    rxCmdCount++;  // count Serial1 ASCII commands alongside binary CMD frames
                }
                rxAsciiIdx    = 0;
                rxAsciiCommas = 0;
            } else if (rxAsciiIdx < sizeof(rxAsciiLine) - 1) {
                rxAsciiLine[rxAsciiIdx++] = (char)b;
                if (b == ',') rxAsciiCommas++;
            } else {
                // Line too long -> bail, wait for next terminator.
                rxAsciiIdx    = 0;
                rxAsciiCommas = 0;
            }
            continue;
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
                    rxDropCount++;
                    DBG_SERIAL_PRINTF("[RX DROP] type=0x%02X len=%u crc=not_checked\n", frameType, payloadLen);
                    resyncFromBuffer();
                    continue;
                }

                if (st.index >= expectedFrameSize) {
                    uint8_t* payload = &st.buffer[4];

                    if (st.buffer[expectedFrameSize - 1] != Proto::EOF_BYTE) {
                        uint16_t calcCrc = Proto::crc16_ccitt(&st.buffer[1], 3 + payloadLen);
                        (void)calcCrc;
                        rxDropCount++;
                        DBG_SERIAL_PRINTF("[RX DROP] len=%u crc=0x%04X\n", payloadLen, calcCrc);
                        resyncFromBuffer();
                        continue;
                    }

                    uint16_t rxCrc = st.buffer[4 + payloadLen] | (st.buffer[5 + payloadLen] << 8);
                    uint16_t calcCrc = Proto::crc16_ccitt(&st.buffer[1], 3 + payloadLen);
                    (void)calcCrc;
                    if (rxCrc != calcCrc) {
                        rxDropCount++;
                        DBG_SERIAL_PRINTF("[RX DROP] len=%u crc=fail (rx=0x%04X calc=0x%04X)\n", payloadLen, rxCrc, calcCrc);
                        resyncFromBuffer();
                        continue;
                    }

                    if (frameType == Proto::TYPE_FFT && allowFFT && payloadLen == Proto::FIXED_PAYLOAD_LEN) {
                        rxFftCount++;
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
                        // applyCommandPayload already prints [CMD] mode=... pattern=... brightness=...
                        // on every successful command (see its impl). No need for a second print here.
                        rxCmdCount++;
                        applyCommandPayload(payload);
                    } else if (frameType == Proto::TYPE_AUX && allowAUX && payloadLen == Proto::AUX_PAYLOAD_LEN) {
                        rxAuxCount++;
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
    parseSerialFrames(SERIAL_CMD, rxCmd, false, false, true);  // CMD only from ESP32 (Serial1/pin 0)
    parseSerialFrames(SERIAL_FFT, rxFFT, true,  true,  false); // FFT+AUX from FFT Teensy (Serial4/pin 16)
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

// Apply a TYPE_CMD payload (shared by Serial1 and USB commands).
// payload[3] is the user-facing brightness in 0..kBrightnessStepMax scale
// (the ESP32 sends in this scale). switchToMode() does the
// mapBrightnessInputToRaw() conversion internally, so we pass the raw
// input through unchanged here — mapping in BOTH places double-applies
// the curve and produces non-linear jumpy brightness steps.
static void applyCommandPayload(const uint8_t* payload) {
    // Command payload (68 bytes):
    // 0: mode, 1: pattern, 3: brightness (0..kBrightnessStepMax input scale)
    char newMode = payload[0];
    int newPattern = payload[1];
    int newBrightness = (int)payload[3];

    switchToMode(newMode, newPattern, newBrightness);
    // Print state.brightness raw (0..255 after mapping) AND the post-power-limit
    // effective value FastLED is actually showing. If raw>0 but eff=0, the
    // FastLED power-limit calc is clamping us to zero — usually means the
    // current leds[] buffer estimates over-budget at this brightness.
    Serial.printf("[CMD] mode=%c pattern=%d state.brt=%u eff=%u (in=%u)\n",
                  state.mode, state.pattern, state.brightness,
                  effectiveBrightnessForCurrentFrame(), (unsigned)payload[3]);
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
    int p2 = 0;
    int p3 = 0;

    int parsed = sscanf(line, " %c,%d,%d,%d", &mode, &p1, &p2, &p3);
    if (mode >= 'a' && mode <= 'z') {
        mode = (char)(mode - 'a' + 'A');
    }
    if (mode == 'O') {
        mode = '0';
    }

    if (parsed < 3 && mode != '0') {
        DBG_SERIAL_PRINTLN("[USB CMD] Parse error");
        return;
    }
    // Both the USB CSV ("M,pattern,brightness") and the ESP32's 4-field
    // form ("M,pattern,brightness,val3") put brightness in the 3rd field
    // (p2). val3 in the 4th field is always 0 in current ESP32 firmware
    // and isn't used here. Earlier code mistakenly read brightness from p3
    // when 4 fields were present, which made every command set brt=0.
    int brightness = p2;
    (void)p3;  // reserved / unused

    if (mode == '0' || mode == 'S' || mode == 'P' || mode == 'M' || mode == 'A') {
        uint8_t payload[Proto::FIXED_PAYLOAD_LEN] = {0};
        payload[0] = static_cast<uint8_t>(mode);
        payload[1] = static_cast<uint8_t>(p1);
        payload[3] = static_cast<uint8_t>(brightness);
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
    if (state.mode == '0') {
        FastLED.clear(true);
    }
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

#if defined(BOARD_LEFT_WS2812B)
void initializeVirtualStripsWS2812BLeft() {
    // Verified physical L->R layout for the WS2812B (left) installation,
    // driven via FastLED's OCTOWS2811 controller. OctoWS2811 has a FIXED
    // channel->pin mapping on Teensy 4:
    //   chain 0 = ch0 = pin 2
    //   chain 1 = ch1 = pin 14
    //   chain 2 = ch2 = pin 7
    //   chain 3 = ch3 = pin 8
    //   chain 4 = ch4 = pin 6
    //   chain 5 = ch5 = pin 20  (NOT WIRED on this board)
    //   chain 6 = ch6 = pin 21
    //   chain 7 = ch7 = pin 5   (NOT WIRED on this board)
    // Pairs (verified 2026-04-07 by strip_id_test):
    //   v 0: pin7  up    v 1: pin7  down
    //   v 2: pin6  down  v 3: pin6  up
    //   v 4: pin21 down  v 5: pin21 up
    //   v 6: pin2  up    v 7: pin2  down
    //   v 8: pin8  up    v 9: pin8  down
    //   v10: pin14 down  v11: pin14 up
    mapVirtualStrip( 0, /*pin7  ch2*/ 2, true  );
    mapVirtualStrip( 1, /*pin7  ch2*/ 2, false );
    mapVirtualStrip( 2, /*pin6  ch4*/ 4, false );
    mapVirtualStrip( 3, /*pin6  ch4*/ 4, true  );
    mapVirtualStrip( 4, /*pin21 ch6*/ 6, false );
    mapVirtualStrip( 5, /*pin21 ch6*/ 6, true  );
    mapVirtualStrip( 6, /*pin2  ch0*/ 0, true  );
    mapVirtualStrip( 7, /*pin2  ch0*/ 0, false );
    mapVirtualStrip( 8, /*pin8  ch3*/ 3, true  );
    mapVirtualStrip( 9, /*pin8  ch3*/ 3, false );
    mapVirtualStrip(10, /*pin14 ch1*/ 1, false );
    mapVirtualStrip(11, /*pin14 ch1*/ 1, true  );
    DBG_SERIAL_PRINTLN("Virtual strips initialized: WS2812B left via OctoWS2811 (12 strips L->R).");
}
#endif

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
    (void)elapsed;
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
            parseSerialFrames(SERIAL_CMD, rxCmd, false, false, true);  // CMD only (Serial1/pin 0)
            parseSerialFrames(SERIAL_FFT, rxFFT, true,  true,  false); // FFT+AUX only (Serial4/pin 16)
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
    SERIAL_CMD.begin(BAUD_RATE);  // CMD from ESP32 on Serial1 RX pin 0
    SERIAL_FFT.begin(BAUD_RATE);  // FFT/AUX from FFT Teensy on Serial4 RX pin 16
    delay(1000);

    loadFireSettings();
    
    DBG_SERIAL_PRINTLN("=== RaveGPT Teensy B: LED Display (SERIAL FFT MODE) ===");

    // ------------------------------------------------------------------
    // Board target is selected at COMPILE time by the PlatformIO env:
    //   pio run -e teensy_led_left  -> -D BOARD_LEFT_WS2812B
    //   pio run -e teensy_led_right -> -D BOARD_RIGHT_APA102
    //
    // The wrong-chipset branch isn't even compiled in, so it's impossible
    // to drive APA102 strips with WS2812B timing (or vice versa) by
    // accident. Build flag is enforced via #error in Globals.h.
    //
    // HW_OCOTP_MAC0 is still printed for reference so each board's chip
    // serial is logged at boot.
    // ------------------------------------------------------------------
    // ------------------------------------------------------------------
    // Boot banner — UNGATED so it survives production builds. Every MCU
    // in this project must print firmware name, build time, USB serial,
    // role, and bus settings so a fresh `pio device monitor` after a
    // reboot can confirm what's actually running on the chip without
    // having to flip debug flags.
    // ------------------------------------------------------------------
    Serial.println();
    Serial.println("================================================================");
    Serial.println("RaveGPT - Teensy B (LED Display Controller)");
    Serial.printf ("  Firmware     : %s\n", __FILE__);
    Serial.printf ("  Build        : %s %s\n", __DATE__, __TIME__);
    Serial.printf ("  USB serial   : %s\n", usb_string_serial_number);
    Serial.printf ("  HW_OCOTP_MAC0: %lu\n", (unsigned long)HW_OCOTP_MAC0);
#if defined(BOARD_LEFT_WS2812B)
    Serial.println("  Role         : LEFT panel  (WS2812B / OctoWS2811)");
#elif defined(BOARD_RIGHT_APA102)
    Serial.println("  Role         : RIGHT panel (APA102 / FastLED + pin14 clock)");
#endif
    Serial.printf ("  CMD bus      : Serial1 RX pin 0   @ %lu baud (ASCII CSV from ESP32)\n", (unsigned long)BAUD_RATE);
    Serial.printf ("  FFT bus      : Serial4 RX pin 16  @ %lu baud (binary frames from FFT Teensy pin 8)\n", (unsigned long)BAUD_RATE);
    Serial.printf ("  Boot mode    : '%c' pattern=%d brightness=%u\n", state.mode, state.pattern, state.brightness);
    Serial.println("================================================================");
    Serial.println();

    // ------------------------------------------------------------------
    // LED hardware init - compile-time branch on board target.
    // ------------------------------------------------------------------
#if defined(BOARD_LEFT_WS2812B)
    // Left side: WS2812B driven via FastLED's OCTOWS2811 controller.
    // OctoWS2811 uses DMA + hardware timers for parallel output across
    // 8 fixed channels (pins 2,14,7,8,6,20,21,5). This is immune to the
    // Serial1 RX ISR jitter that breaks FastLED's bit-banged WS2812B
    // driver at 460800 baud. The leds[] buffer must hold all 8 channels
    // (LED_BUFFER_SIZE = 2304); ch5 (pin 20) and ch7 (pin 5) are not
    // wired on this board but their buffer slots still need to exist.
    FastLED.addLeds<OCTOWS2811>(leds, LEDS_PER_PHYSICAL_STRIP);
#elif defined(BOARD_RIGHT_APA102)
    // Right side: APA102 (4-wire SPI, BGR order, 12 MHz). Six data pins
    // share CLOCK_PIN=14. The carrier board has a 74HCT245 buffer/level
    // shifter on every data line; its OE pin (Teensy pin 3 = BUFFER_ENABLE,
    // active-HIGH) MUST be driven HIGH at boot or the buffer sits in
    // high-Z and the RJ45 cables see no APA102 signal at all. This is the
    // single most common reason "right side stays dark."
    pinMode(BUFFER_ENABLE, OUTPUT);
    digitalWrite(BUFFER_ENABLE, HIGH);

    // Chain order MUST match initializeVirtualStripsAPA102Right(): chain 0
    // = pin 6 (rightmost physical pair), chain 5 = pin 8 (leftmost pair).
    // setCorrection(TypicalLEDStrip) restores the warm tint the original
    // build used; drop it if you want raw RGB.
    FastLED.addLeds<APA102, 6,  CLOCK_PIN, BGR, DATA_RATE_MHZ(12)>(leds,        288).setCorrection(TypicalLEDStrip);
    FastLED.addLeds<APA102, 7,  CLOCK_PIN, BGR, DATA_RATE_MHZ(12)>(leds + 288,  288).setCorrection(TypicalLEDStrip);
    FastLED.addLeds<APA102, 2,  CLOCK_PIN, BGR, DATA_RATE_MHZ(12)>(leds + 576,  288).setCorrection(TypicalLEDStrip);
    FastLED.addLeds<APA102, 21, CLOCK_PIN, BGR, DATA_RATE_MHZ(12)>(leds + 864,  288).setCorrection(TypicalLEDStrip);
    FastLED.addLeds<APA102, 5,  CLOCK_PIN, BGR, DATA_RATE_MHZ(12)>(leds + 1152, 288).setCorrection(TypicalLEDStrip);
    FastLED.addLeds<APA102, 8,  CLOCK_PIN, BGR, DATA_RATE_MHZ(12)>(leds + 1440, 288).setCorrection(TypicalLEDStrip);
#endif

    // Power limiting (kMaxLedPowerMilliWatts is per-side in Globals.h)
    FastLED.setMaxPowerInMilliWatts(kMaxLedPowerMilliWatts);
    // Sync FastLED's internal brightness to whatever state.brightness was
    // initialized to in the global State{} declaration. Without this,
    // FastLED would default to its own initial value (255) and the first
    // frame at boot would be max-bright until the first command arrived.
    FastLED.setBrightness(state.brightness);
    FastLED.clear(true);

#if defined(BOARD_LEFT_WS2812B)
    initializeVirtualStripsWS2812BLeft();
#elif defined(BOARD_RIGHT_APA102)
    initializeVirtualStripsAPA102Right();
#endif

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

    // Two dedicated receive ports, one purpose each:
    //   Serial1 (pin 0 RX)  : CMD frames only, direct from ESP32
    //   Serial4 (pin 16 RX) : FFT + AUX frames only, from FFT Teensy pin 1 TX
    // Framed [0xAA][type][seq][len][payload][crc16][0xBB] @ 460800 baud. A
    // [CMD] line is printed to USB serial each time a command frame is applied.
    parseSerialFrames(SERIAL_CMD, rxCmd, false, false, true);
    parseSerialFrames(SERIAL_FFT, rxFFT, true,  true,  false);

    // 10-second RX status line. Ungated so it's visible in production builds.
    // Counts are summed across both receive ports (Serial1 pin 0 for CMD,
    // Serial4 pin 16 for FFT+AUX). How to read it:
    //   bytes=0            -> nothing arriving on EITHER pin 0 or pin 16
    //                         (check wiring / baud / GND on both)
    //   bytes>0 all 0      -> bytes arriving but never reach a full valid frame
    //                         (usually baud mismatch or stream not framed)
    //   bytes>0 drop>0     -> frames close but failing SOF/len/EOF/CRC check
    //                         (frame format or CRC-poly mismatch)
    //   fft>0 aux>0        -> FFT Teensy alive (audio live ~600/10s, silent ~10/10s heartbeat)
    //   cmd>0              -> ESP32 commands applied
    if (now - lastRxStatusMs >= RX_STATUS_PERIOD_MS) {
        Serial.printf("[STATUS] RX/10s bytes=%lu fft=%lu aux=%lu cmd=%lu drop=%lu\n",
                      (unsigned long)rxByteCount,
                      (unsigned long)rxFftCount,
                      (unsigned long)rxAuxCount,
                      (unsigned long)rxCmdCount,
                      (unsigned long)rxDropCount);
        rxByteCount = 0;
        rxFftCount  = 0;
        rxAuxCount  = 0;
        rxCmdCount  = 0;
        rxDropCount = 0;
        lastRxStatusMs = now;
    }

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
