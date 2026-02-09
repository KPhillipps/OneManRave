#include <Arduino.h>
#include <FastLED.h>
#include "Globals.h"
#include "ColorDefinitions.h"
#include "Patterns.h"  // for CloudParallax_Pattern background reuse

extern uint8_t dominantPitch;
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
// Note hue lookup for pitch classes C..B
// Note-hue helpers are unused for onset-only sparkles; kept for reference

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
// Heavier lift on upper bands; slight trim on lows to balance spectrum
static const float bandGain[BANDS_12] = {
    0.90f, 0.95f, 1.00f, 1.05f, 1.10f, 1.20f,
    1.50f, 1.80f, 2.20f, 2.60f, 3.00f, 3.40f
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

        // Adaptive smoothing: less smoothing on higher bands to let transients show
        float smoothPrev = (i >= 9) ? 0.50f : (i >= 7 ? 0.60f : 0.70f);
        float smoothNew  = 1.0f - smoothPrev;
        bandVis[i] = bandVis[i] * smoothPrev + target * smoothNew;

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
    // Single horizontal VU bar that grows left->right across the 12 strips.
    // Uses globalVis (0..1). Keeps zig-zag layout via virtualLeds[][].
    // Apply a curve to boost across the range and a slight gain on the top.
    float level = constrain(globalVis, 0.0f, 1.0f);
    level = powf(level + 0.02f, 0.60f);                   // lift lows/mids
    level = min(1.0f, level * 1.25f);                     // extra headroom push
    float litF = level * NUM_VIRTUAL_STRIPS;              // fractional lit columns
    int   fullCols = (int)litF;                           // fully lit columns
    float edgeFrac = litF - fullCols;                     // partial column brightness

    // Hue sweep across width for visual interest
    for (int x = 0; x < NUM_VIRTUAL_STRIPS; x++) {
        uint8_t hue = (uint8_t)(x * (255 / max(1, NUM_VIRTUAL_STRIPS - 1)));
        uint8_t val = 0;
        if (x < fullCols) {
            val = 255;
        } else if (x == fullCols && edgeFrac > 0.001f) {
            val = (uint8_t)(edgeFrac * 255.0f);
        } else {
            val = 0;
        }

        for (int y = 0; y < matrix_height; y++) {
            int idx = FLIP_BARS_VERT ? (matrix_height - 1) - y : y;
            *virtualLeds[x][idx] = CHSV(hue, 255, val);
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
// Audio-reactive fire: hot spots injected at bottom travel visibly upward.
// Aggressive cooling keeps baseline low so transients pop as bright bands
// rising through the flame column.
static void renderEQFire() {
    int bands = currentBandCount;
    if (bands < 1) bands = 1;
    if (bands > MAX_BANDS) bands = MAX_BANDS;

    const bool auxFresh = (lastAuxPacketMs != 0 && (millis() - lastAuxPacketMs) < 200);
    const float flux = auxFresh ? (spectralFlux8 / 255.0f) : 0.0f;

    if (!fireInitialized) {
        memset(fireHeat, 0, sizeof(fireHeat));
        fireInitialized = true;
    }

    // Cooling is CONSTANT - never reduced on peaks. This is critical:
    // flames must fall back fast so the next hit creates visible contrast.
    const uint8_t cooling = fireCooling;

    for (int band = 0; band < bands; band++) {
        int strip = band;
        if (strip >= NUM_VIRTUAL_STRIPS) continue;

        float bandLevel = bandVis[band];
        float transient = auxFresh ? (bandDelta8[band] / 255.0f) : 0.0f;

        // Step 1: Cool down - heavier cooling higher up so flames taper.
        // Bottom 20px cool slower (embers persist), upper region cools fast.
        for (int y = 0; y < LEDS_PER_VIRTUAL_STRIP; y++) {
            // Scale cooling: bottom gets 60% of base, top gets 140%
            uint8_t heightScale = 60 + (uint8_t)((uint16_t)y * 80 / LEDS_PER_VIRTUAL_STRIP);
            uint8_t localCool = (uint8_t)(((uint16_t)cooling * heightScale) / 100);
            uint8_t cooldown = random8(0, ((localCool * 10) / LEDS_PER_VIRTUAL_STRIP) + 2);
            fireHeat[strip][y] = qsub8(fireHeat[strip][y], cooldown);
        }

        // Step 2: Heat rises - LIGHT diffusion that preserves hot spots.
        // Instead of averaging 3 cells (which smears everything), shift heat
        // upward with minimal blending so a hot injection travels as a visible
        // bright band moving up the strip.
        for (int y = LEDS_PER_VIRTUAL_STRIP - 1; y >= 3; y--) {
            // 60% from cell below, 25% from 2 below, 15% from 3 below
            // This carries heat upward sharply rather than smearing it
            fireHeat[strip][y] = (uint8_t)(
                ((uint16_t)fireHeat[strip][y - 1] * 155 +
                 (uint16_t)fireHeat[strip][y - 2] * 64 +
                 (uint16_t)fireHeat[strip][y - 3] * 37) >> 8
            );
        }

        // Step 3: Audio sparking - concentrated on real transients only.
        // Low base sparking keeps a dim ember glow; transients inject HOT.
        uint8_t baseChance = fireSparking / 3;  // low idle spark rate
        if (random8() < baseChance) {
            int y = random8(FIRE_SPARK_ZONE);
            fireHeat[strip][y] = qadd8(fireHeat[strip][y], random8(80, 140));
        }

        // Transient-driven burst: this is where reactivity lives.
        // A strong bandDelta injects a concentrated hot band at the bottom
        // that will travel upward as a visible pulse.
        if (transient > 0.08f) {
            uint8_t hotness = (uint8_t)constrain(transient * 400.0f, 120.0f, 255.0f);
            // Inject heat across a narrow zone (3-8 pixels) for a cohesive band
            int sparkCount = 3 + (int)(transient * 12.0f);
            if (sparkCount > 10) sparkCount = 10;
            for (int i = 0; i < sparkCount; i++) {
                int y = random8(0, min(FIRE_SPARK_ZONE, 12));
                fireHeat[strip][y] = qadd8(fireHeat[strip][y], hotness);
            }
        }

        // Band energy adds moderate warmth (sustains flame height during loud passages)
        if (bandLevel > 0.15f) {
            uint8_t warmth = (uint8_t)(bandLevel * 100.0f);
            int y = random8(FIRE_SPARK_ZONE);
            fireHeat[strip][y] = qadd8(fireHeat[strip][y], warmth);
        }

        // Spectral flux burst - big transient across all bands gets extra heat
        if (flux > 0.3f && transient > 0.05f) {
            uint8_t fluxHeat = (uint8_t)constrain(flux * 300.0f, 150.0f, 255.0f);
            int y = random8(0, 8);  // very bottom for maximum travel distance
            fireHeat[strip][y] = qadd8(fireHeat[strip][y], fluxHeat);
        }

        // Step 4: Map heat to fire colors
        for (int y = 0; y < LEDS_PER_VIRTUAL_STRIP; y++) {
            *virtualLeds[strip][y] = ColorFromPalette(firePalette, fireHeat[strip][y]);
        }
    }

    FastLED.show();
}

// ============================================================================
// WLED-Ported Fire Effects (audio-reactive adaptations)
// ============================================================================

// ----------------------------------------------------------------------------
// Noisefire - Perlin noise fire with per-band volume brightness
// Adapted from WLED (Andrew Tuline). Zero-latency audio response.
// fireSparking → noise speed, fireCooling → flame taper, fireAudioBoost → sensitivity
// ----------------------------------------------------------------------------
static void renderNoiseFire() {
    static const CRGBPalette16 noisefirePal(
        CHSV(0,255,2),    CHSV(0,255,4),    CHSV(0,255,8),    CHSV(0,255,8),
        CHSV(0,255,16),   CRGB::Red,         CRGB::Red,         CRGB::Red,
        CRGB::DarkOrange,  CRGB::DarkOrange,  CRGB::Orange,      CRGB::Orange,
        CRGB::Yellow,      CRGB::Orange,      CRGB::Yellow,      CRGB::Yellow);

    const bool auxFresh = (lastAuxPacketMs != 0 && (millis() - lastAuxPacketMs) < 200);
    const uint32_t now = millis();
    const uint8_t speed = fireSparking;            // noise animation speed
    const uint8_t intensity = 255 - fireCooling;   // taper control (high cooling = steep taper)

    for (int strip = 0; strip < NUM_VIRTUAL_STRIPS; strip++) {
        float vol;
        if (auxFresh && strip < MAX_BANDS)
            vol = bandVis8[strip] / 255.0f;
        else if (strip < MAX_BANDS)
            vol = bandVis[strip];
        else
            vol = 0.0f;

        // Transients add extra punch
        float delta = (auxFresh && strip < MAX_BANDS) ? (bandDelta8[strip] / 255.0f) : 0.0f;
        vol += delta * 0.5f;
        vol *= fireAudioBoost;
        // *2 brightness like WLED (volumeSmth*2)
        uint8_t brt = (uint8_t)constrain(vol * 510.0f, 0.0f, 255.0f);

        for (int y = 0; y < LEDS_PER_VIRTUAL_STRIP; y++) {
            // Perlin noise: position along strip × time, offset per strip
            uint16_t nx = (uint16_t)(y * speed / 64 + strip * 1000);
            uint16_t ny = (uint16_t)(now * speed / 64 * LEDS_PER_VIRTUAL_STRIP / 255);
            unsigned idx = inoise8(nx, ny);
            // Taper toward top: y=0 (bottom) bright, y=143 (top) dark
            unsigned taper = 255 - y * 256 / LEDS_PER_VIRTUAL_STRIP;
            unsigned divisor = 256 - intensity;
            if (divisor < 1) divisor = 1;
            idx = taper * idx / divisor;

            *virtualLeds[strip][y] = ColorFromPalette(noisefirePal, idx, brt, LINEARBLEND);
        }
    }

    FastLED.show();
}

// ----------------------------------------------------------------------------
// Fire 2012 - Classic heat simulation with audio-reactive sparking
// Adapted from WLED (Mark Kriegsman). Faithful to original algorithm.
// fireCooling → cooling rate, fireSparking → base spark rate, fireAudioBoost → audio sensitivity
// ----------------------------------------------------------------------------
static void renderFire2012() {
    static uint8_t heat2012[NUM_VIRTUAL_STRIPS][LEDS_PER_VIRTUAL_STRIP];
    static uint32_t lastStep = 0;
    static bool initialized2012 = false;

    if (!initialized2012) {
        memset(heat2012, 0, sizeof(heat2012));
        initialized2012 = true;
    }

    const uint32_t it = millis() >> 5;  // div 32, matches WLED timing
    const bool newStep = (it != lastStep);
    const bool auxFresh = (lastAuxPacketMs != 0 && (millis() - lastAuxPacketMs) < 200);
    const uint8_t ignition = max(3, LEDS_PER_VIRTUAL_STRIP / 10);  // 14 pixels

    for (int strip = 0; strip < NUM_VIRTUAL_STRIPS; strip++) {
        // Per-band audio
        float bandLevel = 0.0f;
        float delta = 0.0f;
        if (strip < MAX_BANDS) {
            bandLevel = auxFresh ? (bandVis8[strip] / 255.0f) : bandVis[strip];
            delta = auxFresh ? (bandDelta8[strip] / 255.0f) : 0.0f;
        }

        // Audio-modulated spark rate
        uint8_t sparkRate = fireSparking;
        sparkRate = qadd8(sparkRate, (uint8_t)constrain(bandLevel * 120.0f * fireAudioBoost, 0.0f, 255.0f));
        sparkRate = qadd8(sparkRate, (uint8_t)constrain(delta * 200.0f * fireAudioBoost, 0.0f, 255.0f));

        // Cooling from fireCooling (NOT reduced by audio - faithful to WLED)
        uint8_t cooling = fireCooling;

        // Step 1: Cool down every cell
        for (int y = 0; y < LEDS_PER_VIRTUAL_STRIP; y++) {
            uint8_t cool = newStep
                ? random8((((20 + cooling / 3) * 16) / LEDS_PER_VIRTUAL_STRIP) + 2)
                : random8(4);
            // Minimum temp in ignition zone (embers never go black)
            uint8_t minTemp = (y < ignition) ? (ignition - y) / 4 + 16 : 0;
            uint8_t temp = qsub8(heat2012[strip][y], cool);
            heat2012[strip][y] = (temp < minTemp) ? minTemp : temp;
        }

        if (newStep) {
            // Step 2: Heat drifts up and diffuses (WLED's exact formula)
            for (int k = LEDS_PER_VIRTUAL_STRIP - 1; k > 1; k--) {
                heat2012[strip][k] = (heat2012[strip][k - 1] + (heat2012[strip][k - 2] << 1)) / 3;
            }

            // Step 3: Randomly ignite sparks (audio-modulated rate)
            if (random8() <= sparkRate) {
                uint8_t y = random8(ignition);
                uint8_t boost = 17 * (ignition - y / 2) / ignition;
                heat2012[strip][y] = qadd8(heat2012[strip][y], random8(96 + 2 * boost, 207 + boost));
            }

            // Extra: audio transient burst (not in original WLED, adds reactivity)
            if (delta > 0.08f) {
                uint8_t hotness = (uint8_t)constrain(delta * 400.0f * fireAudioBoost, 120.0f, 255.0f);
                int count = 2 + (int)(delta * 8.0f);
                if (count > 6) count = 6;
                for (int i = 0; i < count; i++) {
                    uint8_t y = random8(ignition);
                    heat2012[strip][y] = qadd8(heat2012[strip][y], hotness);
                }
            }
        }

        // Step 4: Map heat to fire colors (NOBLEND like WLED)
        for (int y = 0; y < LEDS_PER_VIRTUAL_STRIP; y++) {
            *virtualLeds[strip][y] = ColorFromPalette(firePalette, min(heat2012[strip][y], (uint8_t)240), 255, NOBLEND);
        }
    }

    lastStep = it;
    FastLED.show();
}

// Simple sparkle overlay driven by vocal energy and note
static void overlayVocalSparkles() {
    // Onset: vocal syllable or strong delta in vocal bands
    uint8_t maxDelta = 0;
    for (int b = 3; b <= 7; b++) if (bandDelta8[b] > maxDelta) maxDelta = bandDelta8[b];
    bool onset = vocalSyllable || (maxDelta > 45);
    if (!onset) return;

    // Single burst of ~50 neutral sparkles in a noisy circle
    int count = 50;
    float cx = random8(NUM_VIRTUAL_STRIPS);
    float cy = random16(LEDS_PER_VIRTUAL_STRIP);
    float baseR = 8.0f + (random8() / 255.0f) * 6.0f;
    for (int i = 0; i < count; i++) {
        float ang = (random16() / 65535.0f) * TWO_PI;
        float r   = baseR + ((random8() / 255.0f) * 4.0f) - 2.0f;
        int sx = (int)lroundf(cx + cosf(ang) * r);
        int sy = (int)lroundf(cy + sinf(ang) * r);
        if (sx < 0 || sx >= NUM_VIRTUAL_STRIPS || sy < 0 || sy >= LEDS_PER_VIRTUAL_STRIP) continue;
        CRGB c = CHSV(160, 0, 255); // white/neutral
        CRGB k0 = c;
        CRGB k1 = c; k1.nscale8_video(160);
        CRGB k2 = c; k2.nscale8_video(80);
        nblend(*virtualLeds[sx][sy], k0, 200);
        if (sx > 0)                             nblend(*virtualLeds[sx-1][sy], k1, 96);
        if (sx < NUM_VIRTUAL_STRIPS-1)          nblend(*virtualLeds[sx+1][sy], k1, 96);
        if (sy > 0)                             nblend(*virtualLeds[sx][sy-1], k1, 96);
        if (sy < LEDS_PER_VIRTUAL_STRIP-1)      nblend(*virtualLeds[sx][sy+1], k1, 96);
        if (sx > 0 && sy > 0)                                      nblend(*virtualLeds[sx-1][sy-1], k2, 64);
        if (sx < NUM_VIRTUAL_STRIPS-1 && sy > 0)                   nblend(*virtualLeds[sx+1][sy-1], k2, 64);
        if (sx > 0 && sy < LEDS_PER_VIRTUAL_STRIP-1)               nblend(*virtualLeds[sx-1][sy+1], k2, 64);
        if (sx < NUM_VIRTUAL_STRIPS-1 && sy < LEDS_PER_VIRTUAL_STRIP-1) nblend(*virtualLeds[sx+1][sy+1], k2, 64);
    }
}

// Clustered spark bursts for M7: short dense sparkle bursts on vocal-range peaks
static void overlayClusterSparklesM7() {
    static const int MAX_SPARKS = 160;
    static int sxBuf[160];
    static int syBuf[160];
    static uint8_t lifeBuf[160];
    static int cx, cy;
    static uint8_t burstFrames = 0;          // frames left in current burst
    static unsigned long lastBurstMs = 0;    // cooldown tracking
    static uint8_t burstStrength = 0;        // 0-255, how strong the trigger was

    // --- Decay existing sparks (always, even between bursts) ---
    for (int i = 0; i < MAX_SPARKS; i++) {
        if (!lifeBuf[i]) continue;
        int sx = sxBuf[i];
        int sy = syBuf[i];
        if (sx >= 0 && sx < NUM_VIRTUAL_STRIPS && sy >= 0 && sy < LEDS_PER_VIRTUAL_STRIP) {
            uint8_t v = (uint8_t)(burstStrength >= 128 ? 140 + lifeBuf[i] * 28 : 90 + lifeBuf[i] * 40);
            CRGB spark = CRGB(v, v, v);
            *virtualLeds[sx][sy] += spark;  // additive blend - visible over aurora
        }
        lifeBuf[i]--;
    }

    // --- Peak detection in vocal bands 2-7 (male+female fundamentals+harmonics) ---
    uint8_t maxDelta = 0;
    int     maxBand  = 4;
    for (int b = 2; b <= 7; b++) {
        if (bandDelta8[b] > maxDelta) {
            maxDelta = bandDelta8[b];
            maxBand  = b;
        }
    }

    // Real onset: require meaningful transient, not background noise
    // bandDelta8 range: 0-255, need a real jump (>30) for a burst
    bool onset = false;
    uint8_t strength = 0;
    if (maxDelta > 22) {
        onset = true;
        strength = maxDelta;
    } else if (vocalSyllable && vocalEnv > 60) {
        onset = true;
        strength = vocalEnv;
    } else if (spectralFlux8 > 40 && maxDelta > 15) {
        onset = true;
        strength = (spectralFlux8 + maxDelta) / 2;
    }

    // --- Cooldown: minimum 120ms between bursts ---
    unsigned long now = millis();
    if (onset && burstFrames == 0 && (now - lastBurstMs) > 120) {
        lastBurstMs = now;
        burstStrength = strength;

        // Place cluster center: map band to strip position with jitter
        cx = (maxBand * (NUM_VIRTUAL_STRIPS - 1)) / 11 + (int)((int8_t)random8(5) - 2);
        cx = constrain(cx, 0, NUM_VIRTUAL_STRIPS - 1);
        cy = random16(LEDS_PER_VIRTUAL_STRIP);

        // Fixed-duration burst: 5-10 frames (~80-170ms), NO top-up
        burstFrames = 5 + (strength > 150 ? 5 : (strength > 80 ? 3 : 1));

        // Clear old sparks for fresh burst
        for (int i = 0; i < MAX_SPARKS; i++) lifeBuf[i] = 0;
    }

    if (!burstFrames) return;

    // --- Emit sparks: count and radius scale with peak strength ---
    // strength 30-255 maps to 10-45 sparks per frame
    uint8_t emitCount = 14 + ((uint16_t)burstStrength * 40) / 255 + random8(6);
    // Tight cluster radius: 3-8 pixels (denser clustering)
    float baseR = 3.0f + (burstStrength / 255.0f) * 5.0f;

    for (int e = 0; e < emitCount; e++) {
        int slot = -1;
        for (int i = 0; i < MAX_SPARKS; i++) {
            if (lifeBuf[i] == 0) { slot = i; break; }
        }
        if (slot < 0) break;
        float ang = (random16() / 65535.0f) * TWO_PI;
        float r   = baseR * (0.3f + (random8() / 255.0f) * 0.7f);  // 30-100% of baseR
        sxBuf[slot] = (int)lroundf(cx + cosf(ang) * r);
        syBuf[slot] = (int)lroundf(cy + sinf(ang) * r);
        lifeBuf[slot] = 4 + random8(4); // 4-7 frame lifespan
    }

    burstFrames--;
}



static void renderMusicVisualization() {
    static int lastViz = -1;
    int viz = state.pattern;
    bool reset = (viz != lastViz);
    lastViz = viz;

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
            // Was a flat white left→right bar; remap to colored EQ bars to avoid the all-white look.
            renderEQBarsRainbow();
            break;
        case 5:
            renderEQBarsMono();
            break;
        case 6:
            renderEQFire();
            break;
        case 7:
            // Aurora over clouds with clustered spark bursts on vocal onsets
            CloudParallax_Pattern(reset);
            AuroraOnCloud_Run(reset);
            overlayClusterSparklesM7();
            FastLED.show();
            break;
        case 8:
            // WLED Noisefire: Perlin noise fire, per-band volume brightness
            renderNoiseFire();
            break;
        case 9:
            // Aurora (note sparks) original behavior
            AuroraNoteSparks_Run(reset);
            break;
        case 10:
            // Red Comet with Audio (music visualization)
            RedCometWithAudio1();
            break;
        case 11:
            // Cloud + Aurora + vocal sparkles
            CloudParallax_Pattern(reset);
            AuroraOnCloud_Run(reset);
            overlayVocalSparkles();
            FastLED.show();
            break;
        case 12:
            // WLED Fire 2012: Classic heat sim with audio-reactive sparking
            renderFire2012();
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
