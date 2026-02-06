#include <Arduino.h>
#include <FastLED.h>
#include "Globals.h"
#include "ColorDefinitions.h"
#include "MusicVisualization.h"

// ============================================================================
// Music Visualizations for LED Teensy (receives FFT data via Serial1)
// ============================================================================
// This file contains FFT-reactive visualizations. The FFT processing happens
// on the FFT Teensy; this device receives pre-computed bandAmplitude[] data.

// Forward declaration for effects defined before bandVis[]/globalVis definition
extern float bandVis[MAX_BANDS];
extern float globalVis;

// Matrix dimensions
const int matrix_height = 144; // LEDs per virtual strip

#define COOLING 150
#define SPARKING 80

// Fire2012-style audio-reactive fire effect (runtime-tunable)
static const uint8_t FIRE_SPARK_ZONE = 20;  // Bottom N pixels where sparks can ignite
static uint8_t fireCooling = 75;            // How fast flames cool (higher = shorter flames)
static uint8_t fireSparking = 120;          // Base spark probability (0-255)
static float fireAudioBoost = 1.5f;         // Multiply band amplitude for spark intensity

static const CRGBPalette16 meteoritePalette = CRGBPalette16(
    CRGB(2, 0, 0),    CRGB(6, 0, 0),    CRGB(14, 0, 0),   CRGB(28, 0, 0),
    CRGB(50, 2, 0),   CRGB(80, 6, 0),   CRGB(120, 12, 0), CRGB(170, 24, 0),
    CRGB(220, 50, 0), CRGB(255, 80, 0), CRGB(255, 120, 8), CRGB(255, 160, 25),
    CRGB(255, 200, 60), CRGB(255, 235, 140), CRGB(255, 250, 215), CRGB(255, 255, 255)
);

// Peak sparks: a few per column is enough
struct PeakSpark { float y; float v; uint8_t hue; uint8_t life; bool alive; };
static PeakSpark sparks[NUM_VIRTUAL_STRIPS][3];
static float prevStripLevel[NUM_VIRTUAL_STRIPS] = {0};
static float stripDeltaLocal[NUM_VIRTUAL_STRIPS] = {0};
static uint8_t sparkCooldown[NUM_VIRTUAL_STRIPS] = {0};
static float prevGlobalLevel = 0.0f;

void Fire2012WithAudioEnhanced() {
    // Uses bandAmplitude[] which is updated via Serial1 from FFT Teensy
    static uint8_t heat[NUM_VIRTUAL_STRIPS][LEDS_PER_VIRTUAL_STRIP];

    const bool auxFresh = (lastAuxPacketMs != 0) && (millis() - lastAuxPacketMs < 200);
    float globalLevel = auxFresh ? (globalVis8 / 255.0f) : globalVis;
    float globalDelta = globalLevel - prevGlobalLevel;
    if (globalDelta < 0.0f) globalDelta = 0.0f;
    prevGlobalLevel = globalLevel;

    // Spawn sparks on rising peaks (band index == strip index, 12 bands/strips)
    for (int strip = 0; strip < NUM_VIRTUAL_STRIPS; strip++) {
        float e = (strip < MAX_BANDS) ? bandVis[strip] : 0.0f;
        float delta = e - prevStripLevel[strip];
        prevStripLevel[strip] = e;
        float d = (delta > 0.0f) ? delta : 0.0f;
        if (auxFresh && strip < MAX_BANDS) {
            d = bandDelta8[strip] / 255.0f;
        }
        stripDeltaLocal[strip] = d;

        if (sparkCooldown[strip] > 0) {
            sparkCooldown[strip]--;
        }

        const bool strongPeak = (d > 0.10f) && (e > 0.06f);
        if (strongPeak && sparkCooldown[strip] == 0) {
            for (int i = 0; i < 3; i++) {
                PeakSpark &sp = sparks[strip][i];
                if (!sp.alive) {
                    sp.alive = true;
                    sp.y = (float)random8(2);
                    sp.v = 0.9f + (e * 2.4f) + (d * 2.0f);
                    sp.hue = 8 + (uint8_t)(strip * 4) + (uint8_t)(e * 24.0f);
                    uint16_t life = 180 + (uint16_t)(e * 60.0f) + (uint16_t)(d * 40.0f);
                    sp.life = (life > 255) ? 255 : (uint8_t)life;
                    sparkCooldown[strip] = 5;
                    break;
                }
            }
        }
    }

    for (int strip = 0; strip < NUM_VIRTUAL_STRIPS; strip++) {
        float e = (strip < MAX_BANDS) ? bandVis[strip] : 0.0f;
        float d = (auxFresh && strip < MAX_BANDS) ? (bandDelta8[strip] / 255.0f) : stripDeltaLocal[strip];

        uint8_t cooling = fireCooling;
        int coolBias = (int)(e * 40.0f) + (int)(globalLevel * 25.0f);
        if (coolBias > 0) {
            cooling = (uint8_t)constrain((int)cooling - coolBias, 30, 220);
        }

        // Cool down cells
        for (int i = 0; i < LEDS_PER_VIRTUAL_STRIP; i++) {
            heat[strip][i] = qsub8(heat[strip][i], random8(0, ((cooling * 5) / LEDS_PER_VIRTUAL_STRIP) + 2));
        }

        // Heat drifts upward
        for (int k = LEDS_PER_VIRTUAL_STRIP - 1; k >= 3; k--) {
            heat[strip][k] = (heat[strip][k - 1] * 3 + heat[strip][k - 2] * 2 + heat[strip][k - 3]) / 6;
        }

        uint8_t e8 = (uint8_t)constrain(e * 255.0f, 0.0f, 255.0f);
        uint8_t d8 = (uint8_t)constrain(d * 255.0f, 0.0f, 255.0f);
        uint8_t g8 = (uint8_t)constrain(globalLevel * 255.0f, 0.0f, 255.0f);
        uint8_t flux8 = (uint8_t)constrain((auxFresh ? (spectralFlux8 / 255.0f) : globalDelta) * 255.0f, 0.0f, 255.0f);

        // Random + audio-driven sparks
        uint8_t sparkChance = fireSparking;
        sparkChance = qadd8(sparkChance, scale8(e8, 140));
        sparkChance = qadd8(sparkChance, scale8(d8, 200));
        sparkChance = qadd8(sparkChance, scale8(g8, 60));
        sparkChance = qadd8(sparkChance, scale8(flux8, 80));

        if (random8() < sparkChance) {
            int y = random8(FIRE_SPARK_ZONE);
            uint8_t heatAdd = 140;
            heatAdd = qadd8(heatAdd, scale8(e8, 90));
            heatAdd = qadd8(heatAdd, scale8(d8, 90));
            heatAdd = qadd8(heatAdd, scale8(g8, 40));
            heat[strip][y] = qadd8(heat[strip][y], heatAdd);
        }

        // Map heat to LED colors with flicker
        for (int j = 0; j < LEDS_PER_VIRTUAL_STRIP; j++) {
            CRGB color = HeatColor(heat[strip][j]);
            color.nscale8_video(128 + random8(128)); // Flicker effect
            *virtualLeds[strip][j] = color;
        }
    }

    // Overlay sparks and advance them upward
    for (int strip = 0; strip < NUM_VIRTUAL_STRIPS; strip++) {
        for (int i = 0; i < 3; i++) {
            PeakSpark &sp = sparks[strip][i];
            if (!sp.alive) {
                continue;
            }

            int y = (int)sp.y;
            if (y >= 0 && y < LEDS_PER_VIRTUAL_STRIP) {
                *virtualLeds[strip][y] += CHSV(sp.hue, 200, sp.life);
            }

            sp.y += sp.v;
            sp.v *= 0.96f;
            sp.life = qsub8(sp.life, 12);
            if (sp.y >= LEDS_PER_VIRTUAL_STRIP || sp.life == 0) {
                sp.alive = false;
            }
        }
    }

    FastLED.show();
}

void RedCometWithAudio1() {
    static uint8_t heat[NUM_VIRTUAL_STRIPS][LEDS_PER_VIRTUAL_STRIP];
    const uint8_t cooling = 150;
    const bool auxFresh = (lastAuxPacketMs != 0) && (millis() - lastAuxPacketMs < 200);

    for (int strip = 0; strip < NUM_VIRTUAL_STRIPS; strip++) {
        // Cool down cells to simulate fading effect
        for (int i = 0; i < LEDS_PER_VIRTUAL_STRIP; i++) {
            heat[strip][i] = qsub8(heat[strip][i], random8(0, ((cooling * 3) / LEDS_PER_VIRTUAL_STRIP) + 2));
        }

        // Audio-based comet trigger (band index == strip index, 12 bands/strips)
        int band = strip;
        if (band >= 0 && band < MAX_BANDS) {
            float audioLevel = bandVis[band];
            float delta = auxFresh ? (bandDelta8[band] / 255.0f) : 0.0f;
            float trigger = audioLevel + (delta * 0.7f);
            if (trigger > 0.06f) {
                int base = (int)(LEDS_PER_VIRTUAL_STRIP * 0.2f);
                int peakMin = max(0, base - 10);
                int peakMax = min(LEDS_PER_VIRTUAL_STRIP, base + 10);
                int peakPosition = random8(peakMin, peakMax);
                float intensity = (audioLevel * 2.0f) + (delta * 1.5f);
                heat[strip][peakPosition] = qadd8(heat[strip][peakPosition],
                                                  (uint8_t)constrain(intensity * 255.0f, 0.0f, 255.0f));
            }
        }

        // Propagate the comet upward
        for (int k = LEDS_PER_VIRTUAL_STRIP - 1; k > 2; k--) {
            heat[strip][k] = (heat[strip][k - 1] * 3 + heat[strip][k - 2] * 2 + heat[strip][k - 3]) / 6;
        }

        // Map heat to flame colors
        for (int j = 0; j < LEDS_PER_VIRTUAL_STRIP; j++) {
            CRGB color = HeatColor(heat[strip][j]);
            *virtualLeds[strip][j] = color;
        }
    }

    FastLED.show();
}

// RedCometWithAudio1() is the actual implementation used by renderMusicVisualization()

void meteoriteRain(bool reset) {
    static bool initialized = false;
    static uint32_t lastMs = 0;
    static uint16_t scrollAccumMs = 0;

    // A per-band history column that scrolls downward over time.
    // This creates the "echo" of peaks: top = now, bottom = older.
    static uint8_t trail[NUM_VIRTUAL_STRIPS][LEDS_PER_VIRTUAL_STRIP] = {{0}};

    // Per-band relative peak detection (works at low volume)
    static float maxAmp[NUM_VIRTUAL_STRIPS] = {0};
    static float prevNorm[NUM_VIRTUAL_STRIPS] = {0};
    static uint8_t gate[NUM_VIRTUAL_STRIPS] = {0};          // controls tail duration
    static uint8_t peakCooldown[NUM_VIRTUAL_STRIPS] = {0};  // de-bounce peaks

    if (reset || !initialized) {
        memset(trail, 0, sizeof(trail));
        memset(maxAmp, 0, sizeof(maxAmp));
        memset(prevNorm, 0, sizeof(prevNorm));
        memset(gate, 0, sizeof(gate));
        memset(peakCooldown, 0, sizeof(peakCooldown));
        lastMs = millis();
        scrollAccumMs = 0;
        FastLED.clear();
        FastLED.show();
        initialized = true;
        return;
    }

    processSerialData();
    if (serialDataPending) {
        return;
    }

    uint32_t now = millis();
    uint32_t dtMs = now - lastMs;
    lastMs = now;
    if (dtMs > 100) dtMs = 100;
    {
        uint32_t sum = (uint32_t)scrollAccumMs + dtMs;
        if (sum > 65535u) sum = 65535u;
        scrollAccumMs = (uint16_t)sum;
    }

    int bands = currentBandCount;
    if (bands < 1) bands = 1;
    if (bands > NUM_VIRTUAL_STRIPS) bands = NUM_VIRTUAL_STRIPS;

    uint8_t inject[NUM_VIRTUAL_STRIPS] = {0};

    // Compute injection values (meteor "heads") per band.
    for (int b = 0; b < bands; b++) {
        float raw = bandAmplitude[b];
        if (raw < 0.0f) raw = 0.0f;

        float m = maxAmp[b] * 0.995f;
        if (raw > m) m = raw;
        if (m < 0.5f) m = 0.5f;
        maxAmp[b] = m;

        float norm = raw / m;
        if (norm > 1.0f) norm = 1.0f;

        float rise = norm - prevNorm[b];
        prevNorm[b] = norm;

        if (peakCooldown[b] > 0) peakCooldown[b]--;

        bool peakHit = (peakCooldown[b] == 0 && norm > 0.55f && rise > 0.10f);
        if (peakHit) {
            gate[b] = qadd8(gate[b], (uint8_t)(170 + norm * 80.0f));
            peakCooldown[b] = 2;
        }

        // Keep the tail "emitting" while the band stays hot (duration -> tail length)
        if (norm > 0.50f) {
            gate[b] = qadd8(gate[b], (uint8_t)(6 + norm * 20.0f));
        }

        // Release the gate back to zero
        gate[b] = qsub8(gate[b], (norm > 0.40f) ? 2 : 10);

        uint8_t inj = 0;
        if (gate[b] > 0) {
            inj = (uint8_t)(norm * 255.0f);
            // Small boost so onsets read as bright "heads" without flattening the waveform
            inj = qadd8(inj, scale8(gate[b], 40));
        }
        inject[b] = inj;
    }

    // Scroll speed (ms per pixel). Lower = faster fall.
    const uint8_t stepMs = 16;  // ~60 px/sec
    uint8_t steps = 0;
    while (scrollAccumMs >= stepMs && steps < 6) {
        scrollAccumMs -= stepMs;
        steps++;
    }

    // Advance the history columns.
    if (steps > 0) {
        const uint8_t decay = 246;  // tail fade per pixel-step
        for (uint8_t step = 0; step < steps; step++) {
            processSerialData();
            if (serialDataPending) {
                return;
            }

            for (int x = 0; x < NUM_VIRTUAL_STRIPS; x++) {
                // Shift downward: top (143) moves toward bottom (0)
                for (int y = 0; y < LEDS_PER_VIRTUAL_STRIP - 1; y++) {
                    trail[x][y] = scale8(trail[x][y + 1], decay);
                }
                uint8_t inj = (x < bands) ? inject[x] : 0;
                trail[x][LEDS_PER_VIRTUAL_STRIP - 1] = inj;
            }
        }
    }

    // Render from the history buffer (tail = older values down the strip).
    for (int x = 0; x < NUM_VIRTUAL_STRIPS; x++) {
        if ((x & 0x03) == 0) {
            processSerialData();
            if (serialDataPending) {
                return;
            }
        }

        for (int y = 0; y < LEDS_PER_VIRTUAL_STRIP; y++) {
            uint8_t v = trail[x][y];

            // Deep red background, never fully off.
            uint8_t brt = qadd8(v, 6);
            if (brt < 6) brt = 6;

            uint8_t idx = qadd8(scale8(v, 220), 8);
            CRGB c = ColorFromPalette(meteoritePalette, idx, brt, LINEARBLEND);
            *virtualLeds[x][y] = c;
        }
    }

    FastLED.show();
}

// ============================================================================
// Audio Visualization State
// ============================================================================
float bandVis[MAX_BANDS] = {0.0f};   // 0..1 final per-band height
float globalVis = 0.0f;              // 0..1 overall energy
float beatVis = 0.0f;
float beatFlash = 0.0f;

// Tuning constants
const bool  FLIP_BARS_VERT = false;  // Set true if bars appear inverted

// ============================================================================
// Fire Visualization Constants and State
// ============================================================================
// Fire color palette - warm fire colors (black -> red -> orange -> yellow -> white)
DEFINE_GRADIENT_PALETTE(fireAudio_gp) {
    0,    0,   0,   0,    // black
   32,   32,   0,   0,    // dark red
   64,  128,   0,   0,    // red
   96,  200,  30,   0,    // red-orange
  128,  255,  80,   0,    // orange
  160,  255, 150,   0,    // orange-yellow
  192,  255, 200,  30,    // yellow
  224,  255, 230, 128,    // yellow-white
  255,  255, 255, 200     // white
};
CRGBPalette16 firePalette = fireAudio_gp;

// Fire heat state (per strip)
static uint8_t fireHeat[NUM_VIRTUAL_STRIPS][LEDS_PER_VIRTUAL_STRIP];
static bool fireInitialized = false;

void setFireParams(float boost, uint8_t cooling, uint8_t sparking) {
    fireAudioBoost = constrain(boost, 0.1f, 5.0f);
    fireCooling = (uint8_t)constrain((int)cooling, 0, 255);
    fireSparking = (uint8_t)constrain((int)sparking, 0, 255);
}

void getFireParams(float &boost, uint8_t &cooling, uint8_t &sparking) {
    boost = fireAudioBoost;
    cooling = fireCooling;
    sparking = fireSparking;
}

// ============================================================================
// Compute Visual Bands - with proper scaling
// ============================================================================
// Scaling: bandAmplitude comes in as raw values (0-100+ range)
// We use log scaling to compress dynamic range for better visualization
const float VIS_SCALE = 0.005f;   // Divide raw values (73 → 0.36)
const float VIS_LOG_K = 15.0f;    // Log compression strength

// Per-band gain (low → high). Higher bands get more lift.
static const float bandGain[BANDS_12] = {
    1.00f, 1.00f, 1.05f, 1.10f, 1.10f, 1.15f,
    1.25f, 1.35f, 1.50f, 1.70f, 1.90f, 2.10f
};

// Returns band count for Music mode pattern (always 12 bands).
int getBandCountForPattern(int pattern) {
    (void)pattern;
    // Always use all 12 bands (no reserved end strips).
    return BANDS_12;
}

void computeVisualBands() {
    if (lastAuxPacketMs != 0 && millis() - lastAuxPacketMs < 200) {
        for (int i = 0; i < BANDS_12; i++) {
            bandVis[i] = bandVis8[i] / 255.0f;
        }
        globalVis = globalVis8 / 255.0f;
        beatVis = 0.0f;
        return;
    }

    int bands = currentBandCount;
    if (bands < 1) bands = 1;
    if (bands > MAX_BANDS) bands = MAX_BANDS;

    float sumSq = 0.0f;

    for (int i = 0; i < bands; i++) {
        float raw = bandAmplitude[i];
        if (i < BANDS_12) {
            raw *= bandGain[i];
        }

        // Scale down and apply log compression for better dynamic range
        float scaled = raw * VIS_SCALE;
        float compressed = log1pf(VIS_LOG_K * scaled) / log1pf(VIS_LOG_K);
        float target = constrain(compressed, 0.0f, 1.0f);

        // Light smoothing for stability
        bandVis[i] = bandVis[i] * 0.7f + target * 0.3f;

        sumSq += bandVis[i] * bandVis[i];
    }

    float g = sqrtf(sumSq / bands);
    globalVis = constrain(g, 0.0f, 1.0f);

    // Beat detection disabled (no beat data in payload)
    beatVis = 0.0f;
    beatFlash = 0.0f;
}

static void dumpVizTable() {
    static unsigned long lastDump = 0;
    if (millis() - lastDump < 500) return;
    lastDump = millis();

    DBG_SERIAL_PRINT("[VIZ] ");
    for (int i = 0; i < currentBandCount; i++) {
        DBG_SERIAL_PRINTF("%.2f>%.2f ", bandAmplitude[i], bandVis[i]);
    }
    DBG_SERIAL_PRINTLN();
}

// ============================================================================
// Music Visualization Renderers (uses computed bandVis[])
// ============================================================================
static void renderEQBarsBasic() {
    int bands = currentBandCount;
    if (bands < 1) bands = 1;
    if (bands > MAX_BANDS) bands = MAX_BANDS;

    int stripStart = 0;
    int stripLimit = NUM_VIRTUAL_STRIPS;

    // Peak indicators that fall with gravity
    static float peakPos[MAX_BANDS] = {0};
    static float peakVel[MAX_BANDS] = {0};
    static uint8_t peakHold[MAX_BANDS] = {0};
    const float peakGravity = 0.35f;   // higher = faster fall
    const float peakKick = 0.0f;       // optional upward kick on hit
    const uint8_t peakHoldFrames = 20; // hang time before falling (~20 frames)
    const float peakMin = 0.0f;
    const float peakMax = (float)(matrix_height - 1);

    for (int band = 0; band < bands; band++) {
        int strip = stripStart + band;
        if (strip >= stripLimit) continue;

        int h = (int)(bandVis[band] * matrix_height);
        float target = (float)constrain(h, 0, matrix_height - 1);

        // Update peak physics
        if (target >= peakPos[band]) {
            peakPos[band] = target;
            peakVel[band] = peakKick;
            peakHold[band] = peakHoldFrames;
        } else {
            if (peakHold[band] > 0) {
                peakHold[band]--;
            } else {
                peakVel[band] -= peakGravity;
                peakPos[band] += peakVel[band];
            }
            if (peakPos[band] < peakMin) {
                peakPos[band] = peakMin;
                peakVel[band] = 0.0f;
            }
        }

        for (int y = 0; y < matrix_height; y++) {
            int idx = FLIP_BARS_VERT ? (matrix_height - 1) - y : y;
            if (y < h) {
                uint8_t hue = 96 - y * 2;  // Green at bottom, red at top
                *virtualLeds[strip][idx] = CHSV(hue, 255, 255);
            } else {
                *virtualLeds[strip][idx] = CRGB::Black;
            }
        }

        // Draw peak indicator
        int peakY = (int)roundf(constrain(peakPos[band], peakMin, peakMax));
        int peakIdx = FLIP_BARS_VERT ? (matrix_height - 1) - peakY : peakY;
        *virtualLeds[strip][peakIdx] = CRGB::White;
    }

    FastLED.show();
}

static void renderEQBarsRainbow() {
    int bands = currentBandCount;
    if (bands < 1) bands = 1;
    if (bands > MAX_BANDS) bands = MAX_BANDS;

    int stripStart = 0;
    int stripLimit = NUM_VIRTUAL_STRIPS;

    for (int band = 0; band < bands; band++) {
        int strip = stripStart + band;
        if (strip >= stripLimit) continue;

        int h = (int)(bandVis[band] * matrix_height);
        uint8_t baseHue = (uint8_t)(band * (255 / max(bands, 1)));

        for (int y = 0; y < matrix_height; y++) {
            int idx = FLIP_BARS_VERT ? (matrix_height - 1) - y : y;
            if (y < h) {
                uint8_t hue = baseHue + (uint8_t)(y * 2);
                *virtualLeds[strip][idx] = CHSV(hue, 255, 255);
            } else {
                *virtualLeds[strip][idx] = CRGB::Black;
            }
        }
    }

    FastLED.show();
}

static void renderEQBarsCenter() {
    int bands = currentBandCount;
    if (bands < 1) bands = 1;
    if (bands > MAX_BANDS) bands = MAX_BANDS;

    int stripStart = 0;
    int stripLimit = NUM_VIRTUAL_STRIPS;
    int center = (matrix_height - 1) / 2;
    int halfHeight = matrix_height / 2;

    for (int band = 0; band < bands; band++) {
        int strip = stripStart + band;
        if (strip >= stripLimit) continue;

        int h = (int)roundf(bandVis[band] * halfHeight);
        uint8_t hue = 120 - (uint8_t)(band * 8);

        for (int y = 0; y < matrix_height; y++) {
            int idx = FLIP_BARS_VERT ? (matrix_height - 1) - y : y;
            int dist = abs(y - center);
            if (dist < h) {
                *virtualLeds[strip][idx] = CHSV(hue, 255, 255);
            } else {
                *virtualLeds[strip][idx] = CRGB::Black;
            }
        }
    }

    FastLED.show();
}

static void renderEQPeakDots() {
    int bands = currentBandCount;
    if (bands < 1) bands = 1;
    if (bands > MAX_BANDS) bands = MAX_BANDS;

    int stripStart = 0;
    int stripLimit = NUM_VIRTUAL_STRIPS;

    for (int band = 0; band < bands; band++) {
        int strip = stripStart + band;
        if (strip >= stripLimit) continue;

        int peak = (int)roundf(bandVis[band] * (matrix_height - 1));
        uint8_t hue = 160 + (uint8_t)(band * 7);

        for (int y = 0; y < matrix_height; y++) {
            int idx = FLIP_BARS_VERT ? (matrix_height - 1) - y : y;
            if (y == peak) {
                *virtualLeds[strip][idx] = CHSV(hue, 255, 255);
            } else if (y < peak && y >= peak - 3) {
                *virtualLeds[strip][idx] = CHSV(hue, 255, 120);
            } else {
                *virtualLeds[strip][idx] = CRGB::Black;
            }
        }
    }

    FastLED.show();
}

static void renderEQPulseColumns() {
    int bands = currentBandCount;
    if (bands < 1) bands = 1;
    if (bands > MAX_BANDS) bands = MAX_BANDS;

    int stripStart = 0;
    int stripLimit = NUM_VIRTUAL_STRIPS;

    for (int band = 0; band < bands; band++) {
        int strip = stripStart + band;
        if (strip >= stripLimit) continue;

        uint8_t hue = (uint8_t)(band * (255 / max(bands, 1)));
        uint8_t val = (uint8_t)(bandVis[band] * 255.0f);

        for (int y = 0; y < matrix_height; y++) {
            int idx = FLIP_BARS_VERT ? (matrix_height - 1) - y : y;
            *virtualLeds[strip][idx] = CHSV(hue, 255, val);
        }
    }

    FastLED.show();
}

static void renderEQBarsMono() {
    int bands = currentBandCount;
    if (bands < 1) bands = 1;
    if (bands > MAX_BANDS) bands = MAX_BANDS;

    int stripStart = 0;
    int stripLimit = NUM_VIRTUAL_STRIPS;

    uint8_t boost = (beatFlash > 0.01f) ? (uint8_t)(beatFlash * 80.0f) : 0;

    for (int band = 0; band < bands; band++) {
        int strip = stripStart + band;
        if (strip >= stripLimit) continue;

        int h = (int)(bandVis[band] * matrix_height);
        uint8_t val = (uint8_t)min(255, 180 + boost);

        for (int y = 0; y < matrix_height; y++) {
            int idx = FLIP_BARS_VERT ? (matrix_height - 1) - y : y;
            if (y < h) {
                *virtualLeds[strip][idx] = CHSV(0, 0, val);
            } else {
                *virtualLeds[strip][idx] = CRGB::Black;
            }
        }
    }

    FastLED.show();
}

// ============================================================================
// Fire2012-style Audio-Reactive Fire Visualization
// ============================================================================
// Each strip's fire intensity is driven by its corresponding frequency band.
// Uses proper heat diffusion for realistic flame movement.
static void renderEQFire() {
    int bands = currentBandCount;
    if (bands < 1) bands = 1;
    if (bands > MAX_BANDS) bands = MAX_BANDS;

    int stripStart = 0;
    int stripLimit = NUM_VIRTUAL_STRIPS;

    const bool auxFresh = (lastAuxPacketMs != 0 && (millis() - lastAuxPacketMs) < 200);
    const float flux = auxFresh ? (spectralFlux8 / 255.0f) : 0.0f;
    const float peakMag = auxFresh ? (majorPeakMag / 255.0f) : 0.0f;
    const bool peakGate = auxFresh && (peakDetected != 0);

    // Initialize heat array on first run
    if (!fireInitialized) {
        memset(fireHeat, 0, sizeof(fireHeat));
        fireInitialized = true;
    }

    static float bandPeak[MAX_BANDS] = {0};

    // Cooling reduction on peaks/flux for taller flames
    uint8_t coolingAmount = fireCooling;
    if (peakGate) {
        coolingAmount = (uint8_t)max(5, (int)coolingAmount - 30);
    }
    if (flux > 0.4f) {
        coolingAmount = (uint8_t)max(5, (int)coolingAmount - (int)(flux * 40.0f));
    }

    for (int band = 0; band < bands; band++) {
        int strip = stripStart + band;
        if (strip >= stripLimit) continue;

        float bandLevel = bandVis[band];
        float transient = auxFresh ? (bandDelta8[band] / 255.0f) : 0.0f;
        float burst = (transient * 1.5f) + (flux * 0.6f) + (peakMag * 0.4f);
        // Track a slow-decaying peak so spikes push flames higher
        bandPeak[band] *= 0.94f;
        if (bandLevel > bandPeak[band]) {
            bandPeak[band] = bandLevel;
        }
        if (bandPeak[band] < 0.05f) bandPeak[band] = 0.05f;
        float peakRatio = bandLevel / bandPeak[band];  // 0..1 relative peak

        // Step 1: Cool down every cell a little
        uint8_t bandCooling = coolingAmount;
        if (peakRatio > 0.75f) {
            bandCooling = (uint8_t)max(5, (int)coolingAmount - (int)(peakRatio * 40.0f));
        }
        for (int y = 0; y < LEDS_PER_VIRTUAL_STRIP; y++) {
            // More cooling at top (flames taper naturally)
            uint8_t cooldown = random8(0, ((bandCooling * 10) / LEDS_PER_VIRTUAL_STRIP) + 2);
            fireHeat[strip][y] = qsub8(fireHeat[strip][y], cooldown);
        }

        // Step 2: Heat rises - diffuse upward
        for (int y = LEDS_PER_VIRTUAL_STRIP - 1; y >= 2; y--) {
            fireHeat[strip][y] = (fireHeat[strip][y - 1] + fireHeat[strip][y - 2] + fireHeat[strip][y - 2]) / 3;
        }

        // Step 3: Audio-driven sparking at the bottom
        // Spark intensity scales with band amplitude + transients + flux
        uint8_t sparkIntensity = (uint8_t)constrain((bandLevel * fireAudioBoost + burst) * 255.0f, 0.0f, 255.0f);
        if (sparkIntensity > 255) sparkIntensity = 255;

        // Base sparking + audio boost
        uint8_t sparkChance = fireSparking;
        sparkChance = qadd8(sparkChance, (uint8_t)(bandLevel * 120.0f));
        sparkChance = qadd8(sparkChance, (uint8_t)(transient * 140.0f));
        sparkChance = qadd8(sparkChance, (uint8_t)(flux * 60.0f));
        if (peakGate) sparkChance = qadd8(sparkChance, 30);
        if (peakRatio > 0.75f) {
            sparkChance = qadd8(sparkChance, (uint8_t)(peakRatio * 60.0f));
        }
        if (random8() < sparkChance) {
            int y = random8(FIRE_SPARK_ZONE);
            // Spark intensity based on audio level
            uint8_t newHeat = (sparkIntensity > 100) ? sparkIntensity : random8(160, 255);
            if (peakRatio > 0.8f) {
                newHeat = qadd8(newHeat, (uint8_t)(peakRatio * 80.0f));
            }
            if (peakGate) {
                newHeat = qadd8(newHeat, 40);
            }
            fireHeat[strip][y] = qadd8(fireHeat[strip][y], newHeat);
        }

        // Extra sparks on loud bands or transients
        if (bandLevel > 0.3f || transient > 0.2f) {
            int extraSparks = (int)(bandLevel * 4 + transient * 3);
            for (int i = 0; i < extraSparks; i++) {
                int y = random8(FIRE_SPARK_ZONE);
                fireHeat[strip][y] = qadd8(fireHeat[strip][y], random8(180, 255));
            }
        }

        // Peak echo: inject a hotter spark to travel farther upward
        if (peakRatio > 0.85f && random8() < 180) {
            int y = random8(FIRE_SPARK_ZONE);
            uint8_t echoHeat = (uint8_t)min(255, 200 + (int)(peakRatio * 55.0f));
            fireHeat[strip][y] = qadd8(fireHeat[strip][y], echoHeat);
        }

        // Flux burst - extra sparks across all strips
        if (flux > 0.5f && random8() < 200) {
            int bursts = 1 + (int)(flux * 3.0f);
            for (int i = 0; i < bursts; i++) {
                int y = random8(FIRE_SPARK_ZONE);
                fireHeat[strip][y] = qadd8(fireHeat[strip][y], 255);
            }
        }

        // Step 4: Map heat to fire colors
        for (int y = 0; y < LEDS_PER_VIRTUAL_STRIP; y++) {
            // Use the fire palette for realistic colors
            *virtualLeds[strip][y] = ColorFromPalette(firePalette, fireHeat[strip][y]);
        }
    }

    FastLED.show();
}

static void renderMusicVisualization() {
    int viz = state.pattern;

    switch (viz) {
        case 0:
            renderEQBarsBasic();
            break;
        case 1:
            renderEQBarsRainbow();
            break;
        case 2:
            renderEQBarsCenter();
            break;
        case 3:
            renderEQPeakDots();
            break;
        case 4:
            renderEQPulseColumns();
            break;
        case 5:
            renderEQBarsMono();
            break;
        case 6:
            renderEQFire();
            break;
        case 7:
            AuroraOrganic_Run(false);
            break;
        case 8:
            // Fire2012 with audio-driven sparks
            Fire2012WithAudioEnhanced();
            break;
        case 9:
            AuroraNoteSparks_Run(false);
            break;
        case 10:
            // Meteorite Rain (sound-reactive peak echoes)
            meteoriteRain(false);
            break;
        case 11:
            // Red Comet with Audio (music visualization)
            RedCometWithAudio1();
            break;
        default:
            renderEQBarsBasic();
            break;
    }
}

// ============================================================================
// Map Amplitudes to LEDs (Main Entry Point)
// ============================================================================
void mapAmplitudesToLEDs() {
    currentBandCount = getBandCountForPattern(state.pattern);
    computeVisualBands();
    dumpVizTable();
    renderMusicVisualization();
}
