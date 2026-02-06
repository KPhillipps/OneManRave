#include <Arduino.h>
#include "Globals.h"
#define FASTLED_SPI_SPEED 24  // 24MHz SPI for APA102
#include <FastLED.h>
#include "ColorDefinitions.h"
#include "Patterns.h"
#include "Art.h"

// ============================================================================
// RaveGPT - Teensy B: LED Display Controller (SLAVE)
// ============================================================================
// This is a SLAVE device - NO LOCAL UI OR KEYBOARD INPUT.
// All commands come from the FFT Teensy (master) via Serial1.
// USB Serial is for debug output ONLY.
//
// ========================== SERIAL1 PROTOCOL ==========================
// Two protocols on Serial1 @ 460800 baud:
//
// 1. BINARY FFT DATA (60Hz, from FFT Teensy)
//    Frame: [0xAA][type][seq][len][payload][crc16][0xBB]
//    - type=0x01: FFT payload (45 bytes): 10 floats + beat float + spdif byte
//    - type=0x02: CMD payload (5 bytes): mode, pattern, colorIndex, brightness, flags
//
// 2. ASCII COMMANDS (rare, forwarded from ESP master via FFT Teensy)
//    Format: "M,pattern,colorIndex,brightness\n"
//    Examples:
//      "M,0,0,100\n"  = Music mode, brightness 100
//      "S,5,0,80\n"   = Solid color index 5, brightness 80
//      "P,4,0,100\n"  = Pattern 4 (Fire), brightness 100
//      "0,0,0,0\n"    = Off
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
CRGB* virtualLeds[NUM_VIRTUAL_STRIPS];  // 12 virtual strips (pointers to physical LED sections)
constexpr int LEDL = NUM_APA102_STRIPS * LEDS_PER_PHYSICAL_STRIP;

// State (mode/pattern/brightness)
State state = {'M', 0, 100};  // Default: Music mode using Serial1 FFT
bool firstRun = false;
bool message = true;

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
    static const uint8_t FFT_PAYLOAD_LEN = 45;  // 10 floats + beat float + spdif byte
    static const uint8_t CMD_PAYLOAD_LEN = 5;   // mode, pattern, color, brightness, flags
    static const size_t MAX_FRAME_SIZE = 64;

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
float bandAmplitude[BANDS] = {0};
float beatAmplitude = 0.0f;
uint8_t spdifLock = 0;

// Display settings
const int matrix_width = 10;
const int matrix_height = LEDS_PER_VIRTUAL_STRIP;

// Frame timing
unsigned long lastFrameTime = 0;
unsigned long frameCount = 0;
unsigned long fpsCheckTime = 0;
const unsigned long FRAME_PERIOD_MS = 17;  // ~60 FPS
unsigned long lastPacketTime = 0;

// ============================================================================
// Serial1 Receiver - Binary FFT + ASCII Commands
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
    Serial.printf("%s Mode:%c %s:%d Brt:%d\n", tag, mode, patternLabelForMode(mode), pattern, brightness);
}

// Binary: [SOF=0xAA][type][seq][len][payload][crc16][EOF=0xBB]
// ASCII:  "M,pattern,colorIndex,brightness\n"
// ============================================================================
void processSerialData() {
    static unsigned long lastDebugTime = 0;
    static int packetsReceived = 0;
    static int commandsReceived = 0;
    static int crcErrors = 0;

    while (Serial1.available()) {
        uint8_t b = Serial1.read();

        if (rxIndex == 0) {
            // First byte - determine protocol
            if (b == Proto::SOF) {
                // Binary protocol
                rxBuffer[rxIndex++] = b;
            } else if (b == 'M' || b == 'S' || b == 'P' || b == '0' || b == 'A') {
                // ASCII command - read rest of line
                String line = String((char)b) + Serial1.readStringUntil('\n');
                int p1, p2, p3;
                if (sscanf(line.c_str(), "%*c,%d,%d,%d", &p1, &p2, &p3) == 3) {
                    state.mode = b;
                    state.pattern = p1;
                    state.brightness = p3;
                    FastLED.setBrightness(state.brightness);
                    printControlStatus("[ASCII CMD]", state.mode, state.pattern, state.brightness);
                    commandsReceived++;
                }
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

                if (expectedFrameSize > Proto::MAX_FRAME_SIZE || payloadLen > 50) {
                    // Invalid length, reset
                    rxIndex = 0;
                    continue;
                }

                if (rxIndex >= expectedFrameSize) {
                    // Complete frame received
                    uint8_t frameType = rxBuffer[1];
                    // uint8_t frameSeq = rxBuffer[2];  // Available if needed
                    uint8_t* payload = &rxBuffer[4];

                    // Verify EOF
                    if (rxBuffer[expectedFrameSize - 1] != Proto::EOF_BYTE) {
                        rxIndex = 0;
                        continue;
                    }

                    // Verify CRC (covers type, seq, len, payload)
                    uint16_t rxCrc = rxBuffer[4 + payloadLen] | (rxBuffer[5 + payloadLen] << 8);
                    uint16_t calcCrc = Proto::crc16_ccitt(&rxBuffer[1], 3 + payloadLen);

                    if (rxCrc != calcCrc) {
                        crcErrors++;
                        rxIndex = 0;
                        continue;
                    }

                    // Process based on type
                    if (frameType == Proto::TYPE_FFT && payloadLen == Proto::FFT_PAYLOAD_LEN) {
                        // FFT data: 10 floats + beat float + spdif byte
                        memcpy(bandAmplitude, payload, 40);
                        memcpy(&beatAmplitude, &payload[40], 4);
                        spdifLock = payload[44];
                        lastPacketTime = millis();
                        packetsReceived++;

                        // Debug: Print received values occasionally
                        static unsigned long lastRxDebug = 0;
                        if (millis() - lastRxDebug > 2000) {
                            Serial.print("[RX FFT] ");
                            for (int i = 0; i < 10; i++) {
                                Serial.print(bandAmplitude[i], 4);
                                Serial.print(" ");
                            }
                            Serial.println();
                            lastRxDebug = millis();
                        }
                    } else if (frameType == Proto::TYPE_CMD && payloadLen == Proto::CMD_PAYLOAD_LEN) {
                        // Command: mode, pattern, colorIndex, brightness, flags
                        state.mode = payload[0];
                        state.pattern = payload[1];
                        state.brightness = payload[3];
                        // payload[4] = flags (reserved)

                        FastLED.setBrightness(state.brightness);

                        printControlStatus("[RX CMD]", state.mode, state.pattern, state.brightness);
                        commandsReceived++;
                    }

                    rxIndex = 0;  // Reset for next frame
                }
            }
        }
    }

    // Debug output every second
    if (millis() - lastDebugTime > 3000) {
        if (packetsReceived > 0 || commandsReceived > 0 || crcErrors > 0) {
            Serial.printf("RLED Serial1: %d   FFT, %d CMD, %d CRC errors\n", packetsReceived, commandsReceived, crcErrors);
        }
        packetsReceived = 0;
        commandsReceived = 0;
        crcErrors = 0;
        lastDebugTime = millis();
    }

    // Warn if no FFT packets
    static unsigned long lastNoPacketWarning = 0;
    if (millis() - lastPacketTime > 2000 && millis() - lastNoPacketWarning > 5000) {
        Serial.println("WARNING: No FFT packets from FFT Teensy");
        lastNoPacketWarning = millis();
    }
}

// ============================================================================
// Initialize Virtual LED Strips (from main.cpp)
// ============================================================================
void initializeVirtualStrips() {
    // Each physical strip has 288 LEDs in a zigzag pattern: up 144, down 144
    // We treat each direction as a separate virtual strip (12 virtual strips total)
    
    for (int strip = 0; strip < NUM_APA102_STRIPS; strip++) {
        int baseIndex = strip * LEDS_PER_PHYSICAL_STRIP;
        int virtualStripIndex = strip * 2;
        
        // First virtual strip (LEDs 0-143 going UP)
        virtualLeds[virtualStripIndex] = &leds[baseIndex];
        
        // Second virtual strip (LEDs 144-287 going DOWN) 
        // Data runs forward but represents physical LEDs going down
        virtualLeds[virtualStripIndex + 1] = &leds[baseIndex + LEDS_PER_VIRTUAL_STRIP];
    }

    Serial.println("Virtual strips initialized.");
}

// ============================================================================
// Static Compressor for Audio Visualization (No AGC)
// ============================================================================
// Uses absolute scaling - volume knob changes LED height proportionally.
// Dynamics preserved, high volume doesn't peg, low volume stays visible.

// Visualized audio state
float bandVis[BANDS] = {0.0f};   // 0..1 final per-band height
float globalVis = 0.0f;          // 0..1 overall energy
float beatVis = 0.0f;

// Envelope trackers
float gPeakEnv = 0.0f;
float gAvgEnv  = 0.0f;

// Tuning constants
const float VIS_FLOOR     = 0.10f;   // Quiet baseline (10%)
const float LOG_K         = 40.0f;   // Compression strength (higher = later compression)
const float PEAK_ATTACK   = 0.50f;   // Fast attack for peak envelope
const float PEAK_RELEASE  = 0.08f;   // Slower release for peak envelope
const float AVG_ATTACK    = 0.02f;   // Average envelope attack
const float AVG_RELEASE   = 0.005f;  // Average envelope release
const float PUNCH_GAIN    = 0.35f;   // Transient boost amount
const float EPS           = 1e-6f;   // Epsilon to avoid division by zero
const int   VIS_STEPS     = 5;       // Quantization steps (0 to disable)

// ============================================================================
// Compute Visual Bands (Static Compressor + Transient Punch)
// ============================================================================
void computeVisualBands() {
    // ------------------------------------------------
    // 1) Global RMS loudness (truthful, absolute)
    // ------------------------------------------------
    float sumSq = 0.0f;
    for (int i = 0; i < BANDS; i++) {
        sumSq += bandAmplitude[i] * bandAmplitude[i];
    }
    float g = sqrtf(sumSq / BANDS);

    // ------------------------------------------------
    // 2) Log compression (static, no AGC)
    // ------------------------------------------------
    float visual = log1pf(LOG_K * g) / log1pf(LOG_K);

    // ------------------------------------------------
    // 3) Envelope tracking (transient punch)
    // ------------------------------------------------
    if (g > gPeakEnv)
        gPeakEnv += (g - gPeakEnv) * PEAK_ATTACK;
    else
        gPeakEnv += (g - gPeakEnv) * PEAK_RELEASE;

    if (g > gAvgEnv)
        gAvgEnv += (g - gAvgEnv) * AVG_ATTACK;
    else
        gAvgEnv += (g - gAvgEnv) * AVG_RELEASE;

    float punch = max(0.0f, gPeakEnv - gAvgEnv);
    visual += punch * PUNCH_GAIN;

    // ------------------------------------------------
    // 4) Floor (quiet != dead)
    // ------------------------------------------------
    globalVis = VIS_FLOOR + (1.0f - VIS_FLOOR) * constrain(visual, 0.0f, 1.0f);
    globalVis = constrain(globalVis, 0.0f, 1.0f);

    // ------------------------------------------------
    // 5) Preserve spectral balance
    // ------------------------------------------------
    float scale = globalVis / (g + EPS);

    for (int i = 0; i < BANDS; i++) {
        float v = bandAmplitude[i] * scale;

        // Deadband - suppress noise
        if (v < 0.02f) v = 0.0f;

        // Quantize (intentional steps for cleaner look)
        if (VIS_STEPS > 0) {
            v = roundf(v * VIS_STEPS) / VIS_STEPS;
        }

        bandVis[i] = constrain(v, 0.0f, 1.0f);
    }

    // ------------------------------------------------
    // 6) Beat mapping
    // ------------------------------------------------
    beatVis = constrain(beatAmplitude * 0.8f, 0.0f, 1.0f);
}

// ============================================================================
// Render EQ Bars (uses computed bandVis[])
// ============================================================================
void renderEQBars() {
    for (int band = 0; band < BANDS; band++) {
        int strip = band + 1;
        if (strip >= NUM_VIRTUAL_STRIPS - 1) continue;

        int h = (int)(bandVis[band] * matrix_height);

        for (int y = 0; y < matrix_height; y++) {
            if (y < h) {
                uint8_t hue = 96 - y * 2;  // Green at bottom, red at top
                virtualLeds[strip][y] = CHSV(hue, 255, 255);
            } else {
                virtualLeds[strip][y] = CRGB::Black;
            }
        }
    }
    FastLED.show();
}

// ============================================================================
// Map Amplitudes to LEDs (Main Entry Point)
// ============================================================================
void mapAmplitudesToLEDs() {
    computeVisualBands();
    renderEQBars();
}

// ============================================================================
// Display solid color on all LEDs
// ============================================================================
void displaySolidColor(CRGB color) {
    for (int strip = 0; strip < NUM_VIRTUAL_STRIPS; strip++) {
        for (int led = 0; led < LEDS_PER_VIRTUAL_STRIP; led++) {
            virtualLeds[strip][led] = color;
        }
    }
    FastLED.show();
}

// ============================================================================
// Strip Test - fast rainbow snake through all LEDs
// ============================================================================
void stripTest() {
    Serial.println("Running snake test (1728 LEDs)...");
    unsigned long startTime = millis();

    const int SNAKE_LEN = 2;
    fill_solid(leds, 1728, CRGB::Black);

    for (int pos = 0; pos < 1728 + SNAKE_LEN; pos++) {
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
    Serial.printf("Snake test complete: %lu ms (%d FPS)\n", elapsed, (int)(1748000UL / elapsed));
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
        mapAmplitudesToLEDs();  // Music visualization
    } else if (state.mode == 'A') {
        showArt();
    }
}

// ============================================================================
// Setup
// ============================================================================
void setup() {
    Serial.begin(115200);
    Serial1.begin(BAUD_RATE);
    delay(1000);
    
    Serial.println("=== RaveGPT Teensy B: LED Display (SERIAL FFT MODE) ===");
    Serial.print("Compiled: ");
    Serial.println(__FILE__);
    Serial.println("Reading FFT data from Serial1");
    Serial.println("Starting in Music visualization mode with live audio data\n");

    // Enable 74HCT245 buffer so RJ45 sees the signal
    pinMode(BUFFER_ENABLE, OUTPUT);
    digitalWrite(BUFFER_ENABLE, HIGH);

    // Initialize LED strips - data pins: 2,7,8,6,20,21 with shared clock on pin 14
    FastLED.addLeds<APA102, 2,  CLOCK_PIN, BGR>(leds, 288).setCorrection(TypicalLEDStrip);
    FastLED.addLeds<APA102, 7,  CLOCK_PIN, BGR>(leds + 288, 288).setCorrection(TypicalLEDStrip);
    FastLED.addLeds<APA102, 8,  CLOCK_PIN, BGR>(leds + 576, 288).setCorrection(TypicalLEDStrip);
    FastLED.addLeds<APA102, 6,  CLOCK_PIN, BGR>(leds + 864, 288).setCorrection(TypicalLEDStrip);
    FastLED.addLeds<APA102, 20, CLOCK_PIN, BGR>(leds + 1152, 288).setCorrection(TypicalLEDStrip);
    FastLED.addLeds<APA102, 21, CLOCK_PIN, BGR>(leds + 1440, 288).setCorrection(TypicalLEDStrip);
    
    // Power limiting - 5V @ 50A
    FastLED.setMaxPowerInMilliWatts(250000);  // 5V * 50A = 250W = 250000mW
    FastLED.setBrightness(100);
    
    initializeVirtualStrips();

    Serial.println("FastLED initialized. Running LED test...");
    stripTest();

    Serial.println("LED test complete. Waiting for commands from master...");
}

// ============================================================================
// Main Loop
// ============================================================================
void loop() {
    unsigned long now = millis();

    // Read FFT packets from Serial1
    processSerialData();

    // Render at ~60 FPS
    if (now - lastFrameTime >= FRAME_PERIOD_MS) {
        lastFrameTime = now;
        frameCount++;
        
        // Run current mode
        handleLEDModes();
        
        // Print status every 5 seconds
        if (now - fpsCheckTime >= 5000) {
            fpsCheckTime = now;
            Serial.print("FPS: ");
            Serial.print(frameCount);
            Serial.print(" | Mode: ");
            Serial.print(state.mode);
            Serial.print(" | RLED: ");
            Serial.print(LEDL);
            Serial.print(" | Brightness: ");
            Serial.print(FastLED.getBrightness());
            Serial.print(" | ");
            Serial.print(patternLabelForMode(state.mode));
            Serial.print(": ");
            Serial.print(state.pattern);
            if (state.mode == 'M') {
                Serial.print(" | Bands: ");
                for (int i = 0; i < BANDS; i++) {
                    Serial.print(bandAmplitude[i], 4);  // 4 decimals to match [RX FFT]
                    Serial.print(" ");
                }
                Serial.print(" | Beat: ");
                Serial.print(beatAmplitude, 3);
                Serial.print(" | SPDIF: ");
                Serial.print(spdifLock);
            }
            Serial.println();
            frameCount = 0;
        }
    }
}

// ============================================================================
// End of Teensy B: LED Display Slave
// ============================================================================
