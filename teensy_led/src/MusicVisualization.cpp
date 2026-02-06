#include <Arduino.h>
#include <FastLED.h>
#include "Globals.h"
#include "ColorDefinitions.h"

// ============================================================================
// Music Visualizations for LED Teensy (receives FFT data via Serial1)
// ============================================================================
// This file contains FFT-reactive visualizations. The FFT processing happens
// on the FFT Teensy; this device receives pre-computed bandAmplitude[] data.

// Matrix dimensions
const int matrix_height = 144; // LEDs per virtual strip

// Chroma bins alias (for aurora visualizations)
#ifndef HAS_CHROMA_BINS
static float chromaBinsFallback[12] = {0};
static float* chromaBins = chromaBinsFallback;
#else
extern float chroma[12];
static float* chromaBins = chroma;
#endif


#define COOLING 150
#define SPARKING 80

static const CRGBPalette16 auroraPalette = CRGBPalette16(
    CRGB(2, 4, 20),   CRGB(3, 8, 35),   CRGB(5, 18, 60),  CRGB(0, 40, 90),
    CRGB(0, 70, 120), CRGB(0, 110, 150), CRGB(0, 140, 160), CRGB(10, 170, 170),
    CRGB(40, 200, 180), CRGB(90, 220, 190), CRGB(160, 235, 210), CRGB(220, 240, 230),
    CRGB(255, 255, 235), CRGB(180, 230, 220), CRGB(80, 200, 190), CRGB(10, 120, 150)
);

static const CRGBPalette16 vocalAuroraPalette = CRGBPalette16(
    CRGB(2, 4, 18),   CRGB(4, 8, 28),   CRGB(6, 14, 45),  CRGB(10, 24, 70),
    CRGB(18, 36, 95), CRGB(22, 50, 120), CRGB(30, 70, 150), CRGB(48, 95, 180),
    CRGB(70, 120, 210), CRGB(100, 150, 230), CRGB(130, 180, 240), CRGB(170, 210, 250),
    CRGB(200, 225, 255), CRGB(150, 200, 240), CRGB(90, 160, 220), CRGB(40, 110, 190)
);

static const CRGBPalette16 meteoritePalette = CRGBPalette16(
    CRGB(2, 0, 0),    CRGB(6, 0, 0),    CRGB(14, 0, 0),   CRGB(28, 0, 0),
    CRGB(50, 2, 0),   CRGB(80, 6, 0),   CRGB(120, 12, 0), CRGB(170, 24, 0),
    CRGB(220, 50, 0), CRGB(255, 80, 0), CRGB(255, 120, 8), CRGB(255, 160, 25),
    CRGB(255, 200, 60), CRGB(255, 235, 140), CRGB(255, 250, 215), CRGB(255, 255, 255)
);

// Peak sparks: a few per column is enough
struct PeakSpark { float y; uint8_t hue; uint8_t life; bool alive; };
static PeakSpark sparks[NUM_VIRTUAL_STRIPS][3];
static float prevStripLevel[NUM_VIRTUAL_STRIPS] = {0};
static uint8_t sparkCooldown[NUM_VIRTUAL_STRIPS] = {0};

void Fire2012WithAudioEnhanced() {
    // Uses bandAmplitude[] which is updated via Serial1 from FFT Teensy
    static uint8_t heat[NUM_VIRTUAL_STRIPS][LEDS_PER_VIRTUAL_STRIP];

    // Spawn sparks on rising peaks (band index == strip index, 12 bands/strips)
    for (int strip = 0; strip < NUM_VIRTUAL_STRIPS; strip++) {
        float a = constrain(bandAmplitude[strip], 0.0f, 1.0f);
        float delta = a - prevStripLevel[strip];
        prevStripLevel[strip] = a;

        if (sparkCooldown[strip] > 0) {
            sparkCooldown[strip]--;
        }

        const bool strongPeak = (delta > 0.07f) && (a > 0.12f);
        if (strongPeak && sparkCooldown[strip] == 0) {
            for (int i = 0; i < 3; i++) {
                PeakSpark &sp = sparks[strip][i];
                if (!sp.alive) {
                    sp.alive = true;
                    sp.y = 0.0f;
                    sp.hue = 16 + (uint8_t)(a * 96.0f);
                    sp.life = 220;
                    sparkCooldown[strip] = 6;
                    break;
                }
            }
        }
    }

    for (int strip = 0; strip < NUM_VIRTUAL_STRIPS; strip++) {
        // Cool down cells
        for (int i = 0; i < LEDS_PER_VIRTUAL_STRIP; i++) {
            heat[strip][i] = qsub8(heat[strip][i], random8(0, ((COOLING * 5) / LEDS_PER_VIRTUAL_STRIP) + 2));
        }

        // Heat drifts upward
        for (int k = LEDS_PER_VIRTUAL_STRIP - 1; k >= 2; k--) {
            heat[strip][k] = (heat[strip][k - 1] + heat[strip][k - 2]) / 2;
        }

        // Random sparks
        if (random8() < SPARKING) {
            int y = random8(7);
            heat[strip][y] = qadd8(heat[strip][y], random8(160, 255));
        }

        // Audio-based sparks (band index == strip index, 12 bands/strips)
        int band = strip;
        if (band >= 0 && band < MAX_BANDS) {
            float audioLevel = bandAmplitude[band];
            if (audioLevel > 0.01f) {
                int y = random8(7);
                heat[strip][y] = qadd8(heat[strip][y], (uint8_t)(audioLevel * 255.0f));
            }
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

            sp.y += 1.0f + (sp.life / 128.0f);
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

    for (int strip = 0; strip < NUM_VIRTUAL_STRIPS; strip++) {
        // Cool down cells to simulate fading effect
        for (int i = 0; i < LEDS_PER_VIRTUAL_STRIP; i++) {
            heat[strip][i] = qsub8(heat[strip][i], random8(0, ((cooling * 3) / LEDS_PER_VIRTUAL_STRIP) + 2));
        }

        // Audio-based comet trigger (band index == strip index, 12 bands/strips)
        int band = strip;
        if (band >= 0 && band < MAX_BANDS) {
            float audioLevel = bandAmplitude[band];
            if (audioLevel > 0.01f) {
                int base = (int)(LEDS_PER_VIRTUAL_STRIP * 0.2f);
                int peakMin = max(0, base - 10);
                int peakMax = min(LEDS_PER_VIRTUAL_STRIP, base + 10);
                int peakPosition = random8(peakMin, peakMax);
                heat[strip][peakPosition] = qadd8(heat[strip][peakPosition], (uint8_t)(audioLevel * 255.0f * 2.0f));
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

void vocalAurora(bool reset) {
    static bool initialized = false;
    static uint16_t xOff = 0;
    static uint16_t yOff = 0;
    static uint16_t tOff = 0;
    static uint8_t hueShift = 0;
    static float energy[12] = {0};
    static float bandPeakMax[12] = {0};
    static uint8_t sparkleCooldown[12] = {0};
    static bool blobActive[12] = {0};
    static float blobY[12] = {0};
    static float blobV[12] = {0};
    static int8_t blobDrift[12] = {0};
    static uint8_t sparkleClusterX[12] = {0};
    static uint8_t sparkleClusterY[12] = {0};
    static uint8_t sparkleClusterTimer[12] = {0};
    static uint8_t paletteBias = 0;
    static uint32_t lastMs = 0;
    static uint32_t lastFrameMs = 0;

    if (reset || !initialized) {
        memset(energy, 0, sizeof(energy));
        memset(blobActive, 0, sizeof(blobActive));
        memset(blobY, 0, sizeof(blobY));
        memset(blobV, 0, sizeof(blobV));
        memset(blobDrift, 0, sizeof(blobDrift));
        memset(bandPeakMax, 0, sizeof(bandPeakMax));
        memset(sparkleCooldown, 0, sizeof(sparkleCooldown));
        memset(sparkleClusterX, 0, sizeof(sparkleClusterX));
        memset(sparkleClusterY, 0, sizeof(sparkleClusterY));
        memset(sparkleClusterTimer, 0, sizeof(sparkleClusterTimer));
        xOff = 0;
        yOff = 0;
        tOff = 0;
        hueShift = 0;
        paletteBias = 0;
        lastMs = millis();
        lastFrameMs = 0;
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
    if (now - lastFrameMs < 16) {
        return;  // ~60 FPS
    }
    lastFrameMs = now;

    uint32_t dt = (lastMs == 0) ? 16 : (now - lastMs);
    lastMs = now;

    // Slow upward drift and gentle time evolution
    yOff += (uint16_t)(dt * 2);
    xOff += (uint16_t)(dt * 1);
    tOff += (uint16_t)(dt * 1);

    int bands = currentBandCount;
    if (bands < 1) bands = 1;
    if (bands > 12) bands = 12;

    int sparkleStart = 0;  // use whole field

    float midHighSum = 0.0f;
    for (int i = 0; i < 12; i++) {
        float target = 0.0f;
        if (i < bands) {
            target = bandAmplitude[i] * 0.08f;  // assumes ~0..20 range
            if (target > 1.0f) target = 1.0f;
            if (target < 0.0f) target = 0.0f;
        }

        float prevEnergy = energy[i];
        float alpha = (target > energy[i]) ? 0.4f : 0.08f;  // attack / release
        energy[i] += (target - energy[i]) * alpha;
        if (i >= 4) {
            midHighSum += energy[i];
        }

        if (sparkleCooldown[i] > 0) {
            sparkleCooldown[i]--;
        }

        // Track a slow-decaying per-band peak to make triggers relative (works at low volume)
        float peak = bandPeakMax[i] * 0.96f;
        if (target > peak) peak = target;
        if (peak < 0.08f) peak = 0.08f;  // floor so relative threshold doesn't vanish
        bandPeakMax[i] = peak;

        // Peak detection per-band: spawn dark blob + sparkle clusters
        float rise = target - prevEnergy;
        bool isPeak = (target > peak * 0.65f) && (rise > 0.04f);
        if (isPeak) {
            if (!blobActive[i] || random8() < 160) {
                blobActive[i] = true;
                blobY[i] = (float)(LEDS_PER_VIRTUAL_STRIP - 1);
                blobV[i] = 1.0f + (energy[i] * 2.2f);
                blobDrift[i] = (int8_t)random8(5) - 2;  // -2..+2 drift bias
            }
            bool vocalBand = (i >= 3 && i <= 7);  // mid bands (vocal-ish)
            uint8_t chance = vocalBand ? 245 : 200;
            if (sparkleCooldown[i] == 0 && random8() < chance) {
                sparkleClusterTimer[i] = 4 + random8(vocalBand ? 5 : 3);  // ~60-120ms
                int8_t drift = (int8_t)random8(5) - 2;  // -2..+2
                int xCenter = i + drift;
                if (xCenter < 0) xCenter = 0;
                if (xCenter >= NUM_VIRTUAL_STRIPS) xCenter = NUM_VIRTUAL_STRIPS - 1;
                sparkleClusterX[i] = (int8_t)xCenter;
                sparkleClusterY[i] = (int8_t)random(0, LEDS_PER_VIRTUAL_STRIP);
                sparkleCooldown[i] = 6 + random8(6);
            }
        }

        // Sustain clusters while band is active (helps vocal words)
        if (sparkleClusterTimer[i] > 0) {
            if (target > 0.18f) {
                if (sparkleClusterTimer[i] < 2) {
                    sparkleClusterTimer[i] = 2;
                }
            }
        }
    }


    float brightnessScalar = midHighSum / 8.0f;
    if (brightnessScalar > 1.0f) brightnessScalar = 1.0f;
    if (brightnessScalar < 0.0f) brightnessScalar = 0.0f;

    // Chroma dominance (subtle hue/palette bias)
    float chromaMax = 0.0f;
    int chromaIdx = 0;
    for (int i = 0; i < 12; i++) {
        float c = chromaBins[i];
        if (c > chromaMax) {
            chromaMax = c;
            chromaIdx = i;
        }
    }
    if (chromaMax > 0.35f) {
        paletteBias = (uint8_t)(chromaIdx * 10);
    } else {
        paletteBias = qsub8(paletteBias, 1);
    }

    uint8_t paletteOffset = hueShift + paletteBias;
    hueShift += 1;

    // Render base aurora flow (12 columns)
    for (int x = 0; x < NUM_VIRTUAL_STRIPS; x++) {
        // Column-specific noise offsets
        uint16_t xo = xOff + x * 120;
        uint16_t yo = yOff;
        uint16_t to = tOff;

        for (int y = 0; y < LEDS_PER_VIRTUAL_STRIP; y++) {
            uint8_t n = inoise8(xo, yo + y * 35, to);
            uint8_t idx = qadd8(n, paletteOffset);
            uint8_t brt = scale8(n, (uint8_t)(140 + brightnessScalar * 100.0f));

            // Slight vertical falloff for softer top
            uint8_t falloff = scale8(brt, (uint8_t)(255 - (y * 180 / LEDS_PER_VIRTUAL_STRIP)));
            *virtualLeds[x][y] = ColorFromPalette(vocalAuroraPalette, idx, falloff, LINEARBLEND);
        }
    }

    // Overlay vocal blobs and sparkle clusters
    for (int b = sparkleStart; b < 12; b++) {
        if (blobActive[b]) {
            float y = blobY[b];
            int strip = b;
            if (strip < 0) strip = 0;
            if (strip >= NUM_VIRTUAL_STRIPS) strip = NUM_VIRTUAL_STRIPS - 1;

            for (int k = -2; k <= 2; k++) {
                int yy = (int)y + k;
                if (yy >= 0 && yy < LEDS_PER_VIRTUAL_STRIP) {
                    uint8_t bval = (uint8_t)(200 - abs(k) * 40);
                    *virtualLeds[strip][yy] += CHSV(160 + hueShift, 40, bval);
                }
            }

            blobY[b] -= blobV[b];
            blobV[b] *= 0.98f;
            blobY[b] += (float)blobDrift[b] * 0.15f;
            if (blobY[b] < 0 || blobV[b] < 0.05f) {
                blobActive[b] = false;
            }
        }

        if (sparkleClusterTimer[b] > 0) {
            sparkleClusterTimer[b]--;
            int cx = sparkleClusterX[b];
            int cy = sparkleClusterY[b];
            for (int s = 0; s < 6; s++) {
                int x = cx + (int8_t)random8(3) - 1;
                int y = cy + (int8_t)random8(5) - 2;
                if (x < 0) x = 0;
                if (x >= NUM_VIRTUAL_STRIPS) x = NUM_VIRTUAL_STRIPS - 1;
                if (y < 0) y = 0;
                if (y >= LEDS_PER_VIRTUAL_STRIP) y = LEDS_PER_VIRTUAL_STRIP - 1;
                uint8_t v = (uint8_t)(150 + random8(80));
                *virtualLeds[x][y] += CHSV(140 + hueShift, 30, v);
            }
        }
    }

    FastLED.show();
}

void harmonicAurora(bool reset) {
    static bool initialized = false;
    static uint16_t xOff = 0;
    static uint16_t yOff = 0;
    static uint16_t tOff = 0;
    static uint8_t hueShift = 0;
    static uint32_t lastMs = 0;
    static uint32_t lastFrameMs = 0;
    static float energy[12] = {0};
    static float bandPeakMax[12] = {0};

    if (reset || !initialized) {
        memset(energy, 0, sizeof(energy));
        memset(bandPeakMax, 0, sizeof(bandPeakMax));
        xOff = 0;
        yOff = 0;
        tOff = 0;
        hueShift = 0;
        lastMs = millis();
        lastFrameMs = 0;
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
    if (now - lastFrameMs < 16) {
        return;  // ~60 FPS
    }
    lastFrameMs = now;

    uint32_t dt = (lastMs == 0) ? 16 : (now - lastMs);
    lastMs = now;

    // Slow upward drift and gentle time evolution
    yOff += (uint16_t)(dt * 2);
    xOff += (uint16_t)(dt * 1);
    tOff += (uint16_t)(dt * 1);

    int bands = currentBandCount;
    if (bands < 1) bands = 1;
    if (bands > 12) bands = 12;

    float energySum = 0.0f;
    for (int i = 0; i < 12; i++) {
        float target = 0.0f;
        if (i < bands) {
            target = bandAmplitude[i] * 0.07f;  // assumes ~0..20 range
            if (target > 1.0f) target = 1.0f;
            if (target < 0.0f) target = 0.0f;
        }

        float alpha = (target > energy[i]) ? 0.3f : 0.07f;  // attack / release
        energy[i] += (target - energy[i]) * alpha;
        energySum += energy[i];

        // Track a slow-decaying per-band peak to make triggers relative
        float peak = bandPeakMax[i] * 0.97f;
        if (target > peak) peak = target;
        if (peak < 0.06f) peak = 0.06f;
        bandPeakMax[i] = peak;
    }

    float brightnessScalar = energySum / 9.0f;
    if (brightnessScalar > 1.0f) brightnessScalar = 1.0f;
    if (brightnessScalar < 0.0f) brightnessScalar = 0.0f;

    // Chroma dominance (subtle hue bias)
    float chromaMax = 0.0f;
    int chromaIdx = 0;
    for (int i = 0; i < 12; i++) {
        float c = chromaBins[i];
        if (c > chromaMax) {
            chromaMax = c;
            chromaIdx = i;
        }
    }
    uint8_t paletteOffset = hueShift + (uint8_t)(chromaMax > 0.30f ? chromaIdx * 10 : 0);
    hueShift += 1;

    for (int x = 0; x < NUM_VIRTUAL_STRIPS; x++) {
        uint16_t xo = xOff + x * 110;
        uint16_t yo = yOff;
        uint16_t to = tOff;

        for (int y = 0; y < LEDS_PER_VIRTUAL_STRIP; y++) {
            uint8_t n = inoise8(xo, yo + y * 30, to);
            uint8_t idx = qadd8(n, paletteOffset);
            uint8_t brt = scale8(n, (uint8_t)(120 + brightnessScalar * 120.0f));
            uint8_t falloff = scale8(brt, (uint8_t)(255 - (y * 160 / LEDS_PER_VIRTUAL_STRIP)));
            *virtualLeds[x][y] = ColorFromPalette(auroraPalette, idx, falloff, LINEARBLEND);
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
// Fire2012-style audio-reactive fire effect
static const uint8_t FIRE_SPARK_ZONE = 20;  // Bottom N pixels where sparks can ignite

// Runtime-tunable fire parameters (set defaults here)
static uint8_t fireCooling = 75;    // How fast flames cool (higher = shorter flames)
static uint8_t fireSparking = 120;  // Base spark probability (0-255)
static float fireAudioBoost = 1.5f; // Multiply band amplitude for spark intensity

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
            // Harmonic Aurora (12-band music visualization)
            harmonicAurora(false);
            break;
        case 8:
            // Fire2012 with audio-driven sparks
            Fire2012WithAudioEnhanced();
            break;
        case 9:
            // Vocal Aurora (vocal-only visualization)
            vocalAurora(false);
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
