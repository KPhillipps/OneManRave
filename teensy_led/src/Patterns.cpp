#include "Globals.h"
#include "ColorDefinitions.h"
#include "Patterns.h"
#include <math.h>

// ============================================================================
// Pattern Interruptibility Contract
// ============================================================================
// Patterns are a subcategory of modes and are non-musically reactive in this file.
// This project mixes two styles:
// - Mode 'P' patterns in runPattern() historically use blocking `for (;;)` loops and `delay()`.
// - Mode 'M' visualizations are called once per frame from an outer loop in main.cpp.
//
// When a pattern blocks, the main loop does NOT run, so USB serial commands won't be parsed unless
// the pattern explicitly services input. To avoid "can't be interrupted" regressions:
// - Call patternYield() frequently inside any long loop.
// - Use patternDelay() instead of delay() so we can break immediately on a new command.
// - Prefer frame-based functions `void foo(bool reset=false)` for new patterns.
static inline bool patternYield() {
    return serviceInputs();
}

static inline bool patternDelay(uint16_t ms) {
    return responsiveDelay(ms);
}

void runPattern() {
  static int lastPattern = -1;
  bool reset = (state.pattern != lastPattern);
  if (reset) {
    DBG_SERIAL_PRINTF("Running pattern %d\n", state.pattern);
    lastPattern = state.pattern;
  }
  switch (state.pattern) {
    case 0:
      break;

    case 1:
      break;

    case 2:
      {                          // Rainbow Pattern with Beat
        static uint8_t hue = 0;  // Global hue, incremented over time
        static float beatFlash = 0.0f;
        uint8_t deltaHue = 7;    // Default spacing for rainbow effect

        for (;;) {
          if (patternYield()) {
            break;  // Exit loop on state change
          }

          // Beat detection - flash and speed up on beat
          if (beatAmplitude > 0.15f) {
            beatFlash = 1.0f;
          }
          beatFlash *= 0.85f;  // Decay

          // Calculate brightness boost from beat
          uint8_t baseBrightness = 180;
          uint8_t beatBoost = (uint8_t)(beatFlash * 75);
          uint8_t brightness = baseBrightness + beatBoost;

          // Fill virtualLeds with a rainbow pattern
          for (int strip = 0; strip < NUM_VIRTUAL_STRIPS; strip++) {
            for (int led = 0; led < LEDS_PER_VIRTUAL_STRIP; led++) {
              *virtualLeds[strip][led] = CHSV(hue + (strip * LEDS_PER_VIRTUAL_STRIP + led) * deltaHue, 255, brightness);
            }
          }

          // Flash outer strips white on strong beats
          if (beatFlash > 0.5f) {
            uint8_t flashVal = (uint8_t)(beatFlash * 255);
            for (int led = 0; led < LEDS_PER_VIRTUAL_STRIP; led++) {
              *virtualLeds[0][led] = CHSV(0, 0, flashVal);
              *virtualLeds[NUM_VIRTUAL_STRIPS - 1][led] = CHSV(0, 0, flashVal);
            }
          }

          FastLED.show();
          // Speed up animation on beat
          int frameDelay = beatFlash > 0.3f ? 10 : 20;
          if (patternDelay(frameDelay)) {
            break;
          }
          hue += beatFlash > 0.3f ? 3 : 1;  // Faster hue shift on beat
        }
      }
      break;

    case 3:
      {  // Rainbow with Sparkle + Beat
        static uint8_t hue = 0;
        static float beatFlash = 0.0f;
        uint8_t deltaHue = 7;

        for (;;) {
          if (patternYield()) {
            break;
          }

          // Beat detection
          if (beatAmplitude > 0.15f) {
            beatFlash = 1.0f;
          }
          beatFlash *= 0.88f;

          // More sparkles on beat (base 50, up to 255 on beat)
          uint8_t sparkleChance = 50 + (uint8_t)(beatFlash * 205);
          uint8_t brightness = 180 + (uint8_t)(beatFlash * 75);

          // Fill virtualLeds with a rainbow pattern
          for (int strip = 0; strip < NUM_VIRTUAL_STRIPS; strip++) {
            for (int led = 0; led < LEDS_PER_VIRTUAL_STRIP; led++) {
              *virtualLeds[strip][led] = CHSV(hue + (strip * LEDS_PER_VIRTUAL_STRIP + led) * deltaHue, 255, brightness);
            }
          }

          // Add sparkles - more on beat
          int numSparkles = beatFlash > 0.5f ? 10 : 2;
          for (int s = 0; s < numSparkles; s++) {
            if (random8() < sparkleChance) {
              int randomStrip = random(NUM_VIRTUAL_STRIPS);
              int randomLed = random(LEDS_PER_VIRTUAL_STRIP);
              *virtualLeds[randomStrip][randomLed] = CRGB::White;
            }
          }

          // Flash outer strips on beat
          if (beatFlash > 0.5f) {
            uint8_t flashVal = (uint8_t)(beatFlash * 255);
            for (int led = 0; led < LEDS_PER_VIRTUAL_STRIP; led++) {
              *virtualLeds[0][led] = CHSV(0, 0, flashVal);
              *virtualLeds[NUM_VIRTUAL_STRIPS - 1][led] = CHSV(0, 0, flashVal);
            }
          }

          FastLED.show();
          if (patternDelay(beatFlash > 0.3f ? 10 : 20)) {
            break;
          }
          hue += beatFlash > 0.3f ? 3 : 1;
        }
      }
      break;

    case 4:
      {  // Fire Pattern with Beat
#define COOLING 100
#define SPARKING 180

        static uint8_t heat[NUM_VIRTUAL_STRIPS][LEDS_PER_VIRTUAL_STRIP] = { { 0 } };
        static float beatFlash = 0.0f;

        for (;;) {
          if (patternYield()) {
            break;
          }

          // Beat detection - flare up on beat
          if (beatAmplitude > 0.15f) {
            beatFlash = 1.0f;
          }
          beatFlash *= 0.90f;

          // More sparking on beat
          uint8_t sparkChance = SPARKING + (uint8_t)(beatFlash * 75);

          for (int strip = 0; strip < NUM_VIRTUAL_STRIPS; strip++) {
            // Step 1: Cool down the heat (less cooling on beat for bigger flames)
            uint8_t coolAmount = beatFlash > 0.3f ? COOLING / 2 : COOLING;
            for (int y = 0; y < LEDS_PER_VIRTUAL_STRIP; y++) {
              heat[strip][y] = qsub8(heat[strip][y], random8(0, ((coolAmount * 10) / LEDS_PER_VIRTUAL_STRIP) + 2));
            }

            // Step 2: Heat diffuses upward
            for (int y = LEDS_PER_VIRTUAL_STRIP - 1; y >= 2; y--) {
              heat[strip][y] = (heat[strip][y - 1] + heat[strip][y - 2] + heat[strip][y - 2]) / 3;
            }

            // Step 3: Ignite new sparks at the bottom - more on beat
            if (random8() < sparkChance) {
              int y = random8(7);
              uint8_t sparkIntensity = beatFlash > 0.5f ? 255 : random8(160, 255);
              heat[strip][y] = qadd8(heat[strip][y], sparkIntensity);
            }

            // Extra beat sparks on outer strips
            if (beatFlash > 0.5f && (strip == 0 || strip == NUM_VIRTUAL_STRIPS - 1)) {
              for (int i = 0; i < 5; i++) {
                int y = random8(LEDS_PER_VIRTUAL_STRIP / 3);
                heat[strip][y] = qadd8(heat[strip][y], 255);
              }
            }

            // Step 4: Map heat to colors
            for (int y = 0; y < LEDS_PER_VIRTUAL_STRIP; y++) {
              *virtualLeds[strip][y] = HeatColor(heat[strip][y]);
            }
          }

          FastLED.show();
          if (patternDelay(5)) {
            break;
          }
        }
      }
      break;

    case 5:
      {  // Sinelon Pattern with Beat
        static uint16_t position[NUM_VIRTUAL_STRIPS] = { 0 };
        static uint8_t hue[NUM_VIRTUAL_STRIPS] = { 0 };
        static float beatFlash = 0.0f;

        for (;;) {
          if (patternYield()) {
            break;  // Exit on Serial1 input
          }

          // Beat detection
          if (beatAmplitude > 0.15f) {
            beatFlash = 1.0f;
          }
          beatFlash *= 0.88f;

          // Clear all LEDs (fade less on beat for longer trails)
          uint8_t fadeAmount = beatFlash > 0.3f ? 30 : 60;
          for (int strip = 0; strip < NUM_VIRTUAL_STRIPS; strip++) {
            for (int led = 0; led < LEDS_PER_VIRTUAL_STRIP; led++) {
              virtualLeds[strip][led]->fadeToBlackBy(fadeAmount);
            }
          }

          // Update each strip with the Sinelon effect
          for (int strip = 0; strip < NUM_VIRTUAL_STRIPS; strip++) {
            hue[strip] += beatFlash > 0.3f ? 16 : 8;  // Faster color change on beat
            position[strip] = beatsin16(13 + strip * 2, 0, LEDS_PER_VIRTUAL_STRIP - 1);

            // Highlight the current position on the strip - brighter on beat
            uint8_t brightness = 180 + (uint8_t)(beatFlash * 75);
            *virtualLeds[strip][position[strip]] = CHSV(hue[strip], 255, brightness);

            // Add extra dot on beat
            if (beatFlash > 0.5f) {
              int mirrorPos = LEDS_PER_VIRTUAL_STRIP - 1 - position[strip];
              *virtualLeds[strip][mirrorPos] = CHSV(hue[strip] + 128, 255, (uint8_t)(beatFlash * 200));
            }
          }

          // Flash outer strips on beat
          if (beatFlash > 0.5f) {
            uint8_t flashVal = (uint8_t)(beatFlash * 255);
            for (int led = 0; led < LEDS_PER_VIRTUAL_STRIP; led++) {
              *virtualLeds[0][led] = CHSV(0, 0, flashVal);
              *virtualLeds[NUM_VIRTUAL_STRIPS - 1][led] = CHSV(0, 0, flashVal);
            }
          }

          FastLED.show();
          if (patternDelay(beatFlash > 0.3f ? 8 : 15)) {
            break;
          }
        }
      }
      break;


    case 6:
      {  // Enhanced Sinelon Pattern with Beat
        static uint16_t position[NUM_VIRTUAL_STRIPS] = { 0 };
        static uint8_t hue[NUM_VIRTUAL_STRIPS] = { 0 };
        static float beatFlash = 0.0f;

        for (;;) {
          if (patternYield()) {
            break;
          }

          // Beat detection
          if (beatAmplitude > 0.15f) {
            beatFlash = 1.0f;
          }
          beatFlash *= 0.88f;

          // Longer comet on beat
          int cometLength = beatFlash > 0.3f ? 35 : 20;

          // Clear LEDs
          FastLED.clear();

          // Update each strip
          for (int strip = 0; strip < NUM_VIRTUAL_STRIPS; strip++) {
            hue[strip] += beatFlash > 0.3f ? 16 : 8;
            position[strip] = beatsin16(13 + strip * 2, 0, LEDS_PER_VIRTUAL_STRIP - 1);

            // Create comet - brighter on beat
            uint8_t baseBrightness = 180 + (uint8_t)(beatFlash * 75);
            for (int offset = 0; offset < cometLength; offset++) {
              int trailPosition = (position[strip] - offset + LEDS_PER_VIRTUAL_STRIP) % LEDS_PER_VIRTUAL_STRIP;
              uint8_t brightness = baseBrightness - (offset * (baseBrightness / cometLength));
              *virtualLeds[strip][trailPosition] += CHSV(hue[strip], 255, brightness);
            }

            // Second comet going opposite direction on beat
            if (beatFlash > 0.5f) {
              int mirrorPos = LEDS_PER_VIRTUAL_STRIP - 1 - position[strip];
              for (int offset = 0; offset < cometLength / 2; offset++) {
                int trailPosition = (mirrorPos + offset) % LEDS_PER_VIRTUAL_STRIP;
                uint8_t brightness = (uint8_t)(beatFlash * 200) - (offset * 10);
                if (brightness > 200) brightness = 0;  // Underflow protection
                *virtualLeds[strip][trailPosition] += CHSV(hue[strip] + 128, 255, brightness);
              }
            }
          }

          // Flash outer strips on beat
          if (beatFlash > 0.5f) {
            uint8_t flashVal = (uint8_t)(beatFlash * 255);
            for (int led = 0; led < LEDS_PER_VIRTUAL_STRIP; led++) {
              *virtualLeds[0][led] = CHSV(0, 0, flashVal);
              *virtualLeds[NUM_VIRTUAL_STRIPS - 1][led] = CHSV(0, 0, flashVal);
            }
          }

          FastLED.show();
          if (patternDelay(beatFlash > 0.3f ? 8 : 15)) {
            break;
          }
        }
      }
      break;
    case 7:
      {  // Meteor Shower with Beat - non-blocking version
        static int meteorPos[NUM_VIRTUAL_STRIPS];
        static uint8_t meteorHue[NUM_VIRTUAL_STRIPS];
        static bool meteorActive[NUM_VIRTUAL_STRIPS];
        static float beatFlash = 0.0f;
        static bool initialized = false;

        if (!initialized) {
          for (int strip = 0; strip < NUM_VIRTUAL_STRIPS; strip++) {
            meteorPos[strip] = -1;
            meteorActive[strip] = false;
            meteorHue[strip] = random8();
          }
          FastLED.clear();
          initialized = true;
        }

        for (;;) {
          if (patternYield()) {
            initialized = false;  // Reset on exit
            break;
          }

          // Beat detection - trigger meteors on beat
          if (beatAmplitude > 0.15f) {
            beatFlash = 1.0f;
            // Launch meteors on random strips on beat
            int numToLaunch = random8(3, 6);
            for (int i = 0; i < numToLaunch; i++) {
              int strip = random8(NUM_VIRTUAL_STRIPS);
              if (!meteorActive[strip]) {
                meteorActive[strip] = true;
                meteorPos[strip] = LEDS_PER_VIRTUAL_STRIP + 10;  // Start above top
                meteorHue[strip] = random8();
              }
            }
          }
          beatFlash *= 0.90f;

          // Randomly spawn meteors when idle
          if (random8() < 15) {
            int strip = random8(NUM_VIRTUAL_STRIPS);
            if (!meteorActive[strip]) {
              meteorActive[strip] = true;
              meteorPos[strip] = LEDS_PER_VIRTUAL_STRIP + 10;
              meteorHue[strip] = random8();
            }
          }

          // Fade all LEDs
          uint8_t fadeAmount = beatFlash > 0.3f ? 30 : 50;
          for (int strip = 0; strip < NUM_VIRTUAL_STRIPS; strip++) {
            for (int led = 0; led < LEDS_PER_VIRTUAL_STRIP; led++) {
              virtualLeds[strip][led]->fadeToBlackBy(fadeAmount);
            }
          }

          // Update and draw meteors
          int meteorSize = beatFlash > 0.3f ? 8 : 5;
          for (int strip = 0; strip < NUM_VIRTUAL_STRIPS; strip++) {
            if (meteorActive[strip]) {
              meteorPos[strip] -= 2;  // Move down

              // Draw meteor with gradient tail
              for (int i = 0; i < meteorSize; i++) {
                int pos = meteorPos[strip] + i;
                if (pos >= 0 && pos < LEDS_PER_VIRTUAL_STRIP) {
                  uint8_t brightness = 255 - (i * (200 / meteorSize));
                  *virtualLeds[strip][pos] = CHSV(meteorHue[strip], 255, brightness);
                }
              }

              // Deactivate when off screen
              if (meteorPos[strip] < -meteorSize) {
                meteorActive[strip] = false;
              }
            }
          }

          // Flash outer strips on beat
          if (beatFlash > 0.5f) {
            uint8_t flashVal = (uint8_t)(beatFlash * 255);
            for (int led = 0; led < LEDS_PER_VIRTUAL_STRIP; led++) {
              *virtualLeds[0][led] = CHSV(0, 0, flashVal);
              *virtualLeds[NUM_VIRTUAL_STRIPS - 1][led] = CHSV(0, 0, flashVal);
            }
          }

          FastLED.show();
          if (patternDelay(beatFlash > 0.3f ? 15 : 25)) {
            initialized = false;
            break;
          }
        }
      }
      break;

    case 8:
      {  // Cloud Parallax ambient pattern (non-music)
        CloudParallax_Pattern(reset);
        FastLED.show();
      }
      break;
    default:
      break;
  }
}

// ------------------------------------------------------------------
// Cloud Parallax Pattern (ambient) - deep blue sky with big drifting blobs
// ------------------------------------------------------------------
// This implementation is tuned for a 12x144 matrix: very low-frequency blobs,
// cohesive hue range, and parallax speeds scaled for "slow cloud" motion.

// ---------------- Tunables ----------------
static constexpr uint8_t  ZONES_TOTAL     = 6;   // fewer blobs overall
static constexpr uint8_t  FAR_ZONES       = 2;
static constexpr uint8_t  MID_ZONES       = 2;
static constexpr uint8_t  FORE_ZONES      = 2;

// Speeds tuned so a blob takes ~12–18s to cross 12 columns
static constexpr float    BASE_DRIFT_PX_S = 1.1f;  // px/sec baseline
static constexpr float    FAR_FACTOR      = 0.70f;
static constexpr float    MID_FACTOR      = 1.00f;
static constexpr float    FORE_FACTOR     = 1.30f;

static constexpr uint8_t  SOFT_EDGE       = 96;   // very soft blob edges for gentler fade
static constexpr uint8_t  PERSIST_BLEND   = 0;    // we overwrite backfield each frame
static constexpr float    FADE_IN_SEC     = 2.0f; // cloud fade-in duration
static const char*        CLOUD_PATTERN_VER = "CloudParallax v0.10 2026-02-06";

// Backfield steady light blue with small noise boost
static inline CRGB BackfieldColor(uint8_t noiseVBoost, uint8_t row) {
  // Darker at top, lighter at bottom, 15% deeper overall
  uint8_t grad = scale8(row, 45);   // 0..~45
  uint8_t vBase = 70 + grad;        // brighten base (~70..115)
  uint8_t sat   = 180;              // slightly less saturated
  uint8_t hue   = 166;              // slight shift toward deeper blue
  return CHSV(hue, sat, qadd8(vBase, noiseVBoost));
}

// Light-blue cloud color with slight hue jitter
static inline CRGB CloudColor(uint8_t hueJitter, uint8_t v) {
      uint8_t hue = 162 + (hueJitter % 8);      // very tight hue band
      uint8_t sat = 110 + (hueJitter % 20);     // moderate saturation
  return CHSV(hue, sat, v);
}

enum Layer : uint8_t { FAR=0, MID=1, FORE=2 };

struct Zone {
  float cx, cy;      // center
  float vx, vy;      // velocity
  uint8_t rx, ry;    // radii in px
  uint16_t nseed;    // noise seed for edge wobble
  uint8_t hueJ;      // minor hue jitter
  float fade;        // 0..1 fade-in factor
  Layer layer;
  bool  alive;
};

static struct {
  uint32_t lastMs = 0;
  Zone zones[20];
  uint8_t zoneCount = ZONES_TOTAL;
  uint16_t xOff=0, yOff=0, tOff=0; // backfield noise drift
} cloudG;

static inline CRGB& cloudP(int x,int y){
  if (x < 0) x = 0; else if (x >= NUM_VIRTUAL_STRIPS) x = NUM_VIRTUAL_STRIPS-1;
  if (y < 0) y = 0; else if (y >= LEDS_PER_VIRTUAL_STRIP) y = LEDS_PER_VIRTUAL_STRIP-1;
  return *virtualLeds[x][y];
}

static int wrapBand(int v, int lo, int hi){
  int span = hi - lo + 1;
  while (v < lo) v += span;
  while (v > hi) v -= span;
  return v;
}

static void spawnClouds() {
  uint8_t z = 0;
  auto mk = [&](Layer L, uint8_t count){
    for (uint8_t i=0; i<count && z<cloudG.zoneCount; ++i,++z){
      Zone &q = cloudG.zones[z];
      q.layer = L;
      q.cx = random8(0, NUM_VIRTUAL_STRIPS-1) + 0.5f;
      q.cy = random16(0, LEDS_PER_VIRTUAL_STRIP-1) + 0.5f;

      // Wider than tall for a more natural cloud perspective
      uint8_t rxMin=5, rxMax=9, ryMin=8, ryMax=16;   // moderate size, avoid tiny
      if (L == FAR) { rxMin=5; rxMax=8;  ryMin=8;  ryMax=14; }
      if (L == FORE){ rxMin=6; rxMax=10; ryMin=9;  ryMax=18; } // slightly larger fore blobs
      q.rx = random8(rxMin, rxMax+1);
      // Final clamp to avoid skinny blobs and prevent exceeding panel width
      if (q.rx < 5) q.rx = 5;
      if (q.rx > NUM_VIRTUAL_STRIPS-2) q.rx = NUM_VIRTUAL_STRIPS-2;
      q.ry = random8(ryMin, ryMax+1);

      float base = BASE_DRIFT_PX_S;
      float f = (L==FAR)? FAR_FACTOR : (L==MID? MID_FACTOR : FORE_FACTOR);
      // Single direction (left-to-right) for calmer feel; jitter magnitude only
      q.vx = (base * f) * (0.8f + (random8() / 255.0f) * 0.5f); // 0.8..1.3x
      q.vy = 0.0f;

      q.nseed = random16();
      q.hueJ  = random8();
      q.fade  = 0.0f;
      q.alive = true;
    }
  };
  mk(FAR,  FAR_ZONES);
  mk(MID,  MID_ZONES);
  mk(FORE, FORE_ZONES);
}

static void cloudReset() {
  memset(&cloudG, 0, sizeof(cloudG));
  cloudG.zoneCount = ZONES_TOTAL;
  DBG_SERIAL_PRINTF("%s\n", CLOUD_PATTERN_VER);
  spawnClouds();
}

static void drawBackfield() {
  cloudG.tOff += 1;
  for (int x=0; x<NUM_VIRTUAL_STRIPS; ++x){
    uint16_t xo = cloudG.xOff + x*123;
    for (int y=0; y<LEDS_PER_VIRTUAL_STRIP; ++y){
      uint8_t n = inoise8(xo, cloudG.yOff + y*37, cloudG.tOff);
      CRGB c = BackfieldColor(scale8(n, 70), (uint8_t)y);
      // Overwrite backfield each frame for contrast; no persistence
      cloudP(x,y) = c;
    }
  }
}

static void updateZones(float dt) {
  uint16_t d = (uint16_t)(dt * 1000.0f);
  cloudG.xOff += (uint16_t)(d * 0.12f); // faster drift for animation under music viz
  cloudG.yOff += (uint16_t)(d * 0.06f);

  for (uint8_t i=0;i<cloudG.zoneCount;i++){
    Zone &q = cloudG.zones[i];
    if (!q.alive) continue;

    q.cx += q.vx * dt; // px/sec directly in column units
    if (q.cx < -q.rx) {
      q.cx = NUM_VIRTUAL_STRIPS + q.rx;
      // randomize vertical start and hue to avoid same-origin appearance
      q.cy = random16(q.ry, LEDS_PER_VIRTUAL_STRIP - q.ry) + 0.5f;
      q.hueJ = random8();
      q.nseed = random16();
      q.fade = 0.0f;
    }
    if (q.cx > NUM_VIRTUAL_STRIPS + q.rx) {
      q.cx = -q.rx;
      q.cy = random16(q.ry, LEDS_PER_VIRTUAL_STRIP - q.ry) + 0.5f;
      q.hueJ = random8();
      q.nseed = random16();
      q.fade = 0.0f;
    }

    uint8_t n = inoise8((uint16_t)(q.nseed + cloudG.tOff*2), (uint16_t)(q.cy*4));
    float bob = ((int)n - 128) / 128.0f; // -1..1
    q.cy += bob * 0.14f; // keep some bob but a bit calmer
    if (q.cy < q.ry) q.cy = q.ry;
    if (q.cy > (LEDS_PER_VIRTUAL_STRIP-1 - q.ry)) q.cy = (LEDS_PER_VIRTUAL_STRIP-1 - q.ry);

    // Fade-in ramp
    if (q.fade < 1.0f) {
      q.fade += dt / FADE_IN_SEC;
      if (q.fade > 1.0f) q.fade = 1.0f;
    }
  }
}

static void drawZones() {
  for (uint8_t pass=0; pass<3; ++pass){
    for (uint8_t i=0;i<cloudG.zoneCount;i++){
      Zone &q = cloudG.zones[i];
      if (!q.alive || (uint8_t)q.layer != pass) continue;

      // Skip very narrow blobs (belt-and-suspenders)
      if (q.rx < 6 || q.ry < 6) continue;

      int xmin = (int)floorf(q.cx - q.rx - 1);
      int xmax = (int)ceilf(q.cx + q.rx + 1);
      int ymin = (int)floorf(q.cy - q.ry - 1);
      int ymax = (int)ceilf(q.cy + q.ry + 1);
      xmin = wrapBand(xmin, 0, NUM_VIRTUAL_STRIPS-1);
      xmax = wrapBand(xmax, 0, NUM_VIRTUAL_STRIPS-1);

      auto paintSpan = [&](int xs, int xe){
        for (int x = xs; x <= xe; ++x) {
          for (int y = max(0,ymin); y <= min(LEDS_PER_VIRTUAL_STRIP-1, ymax); ++y) {
            // Wrap-aware horizontal distance so large blobs don’t get clipped at the seam
            float dxRaw = (float)x - q.cx;
            if (dxRaw > (NUM_VIRTUAL_STRIPS * 0.5f))  dxRaw -= NUM_VIRTUAL_STRIPS;
            if (dxRaw < -(NUM_VIRTUAL_STRIPS * 0.5f)) dxRaw += NUM_VIRTUAL_STRIPS;
            float dx = dxRaw / (float)q.rx;
            float dy = (y - q.cy) / (float)q.ry;
            float d2 = dx*dx + dy*dy;
            if (d2 > 3.5f) continue; // slightly wider mask; keeps more body

            // Smooth Gaussian-ish falloff; larger blobs get softer edges (wider sigma)
            float softness = 0.55f * (6.0f / max(3.0f, (float)q.rx)); // softer edge
            float base = expf(-d2 * softness);            // ~1 at center
            uint8_t wn = inoise8((uint16_t)(x*37 + q.nseed), (uint16_t)(y*29 + q.nseed), cloudG.tOff);
            float wobble = ((int)wn - 128) / 2048.0f;   // subtle texture, not pop
            float alphaF = constrain(base + wobble, 0.10f, 1.0f); // lighter minimum rim, wider visible body
            alphaF *= q.fade; // fade-in multiplier
            uint8_t alpha = (uint8_t)(alphaF * 255.0f);

            uint8_t v = (q.layer==FORE) ? 195 : (q.layer==MID ? 182 : 168);
            CRGB c = CloudColor(q.hueJ, v);
            nblend(cloudP(x,y), c, alpha);
          }
        }
      };

      if (xmin <= xmax) {
        paintSpan(xmin, xmax);
      } else {
        paintSpan(xmin, NUM_VIRTUAL_STRIPS-1);
        paintSpan(0, xmax);
      }
    }
  }
}

void CloudParallax_Pattern(bool reset){
  if (reset || cloudG.lastMs==0) { cloudReset(); }

  uint32_t now = millis();
  float dt = (cloudG.lastMs==0) ? 0.016f : (now - cloudG.lastMs)/1000.0f;
  if (dt > 0.03f) dt = 0.03f; // smaller step to reduce column jumps
  cloudG.lastMs = now;

  drawBackfield();
  updateZones(dt);
  drawZones();

  // Medium separable blur: horizontal radius 2 then vertical radius 1
  static CRGB scratch[NUM_VIRTUAL_STRIPS][LEDS_PER_VIRTUAL_STRIP];

  // Horizontal blur (wrap in X), radius 2, weights [1,2,4,2,1]/10
  for (int y = 0; y < LEDS_PER_VIRTUAL_STRIP; ++y) {
    for (int x = 0; x < NUM_VIRTUAL_STRIPS; ++x) {
      CRGB a = *virtualLeds[(x+NUM_VIRTUAL_STRIPS-2)%NUM_VIRTUAL_STRIPS][y];
      CRGB b = *virtualLeds[(x+NUM_VIRTUAL_STRIPS-1)%NUM_VIRTUAL_STRIPS][y];
      CRGB c = *virtualLeds[x][y];
      CRGB d = *virtualLeds[(x+1)%NUM_VIRTUAL_STRIPS][y];
      CRGB e = *virtualLeds[(x+2)%NUM_VIRTUAL_STRIPS][y];
      scratch[x][y].r = (uint8_t)((a.r + 2*b.r + 4*c.r + 2*d.r + e.r) / 10);
      scratch[x][y].g = (uint8_t)((a.g + 2*b.g + 4*c.g + 2*d.g + e.g) / 10);
      scratch[x][y].b = (uint8_t)((a.b + 2*b.b + 4*c.b + 2*d.b + e.b) / 10);
    }
  }

  // Vertical blur (no wrap in Y; clamp edges) radius 1, weights [1,2,1]/4
  for (int x = 0; x < NUM_VIRTUAL_STRIPS; ++x) {
    for (int y = 0; y < LEDS_PER_VIRTUAL_STRIP; ++y) {
      int y0 = y > 0 ? y-1 : 0;
      int y2 = y < LEDS_PER_VIRTUAL_STRIP-1 ? y+1 : LEDS_PER_VIRTUAL_STRIP-1;
      CRGB a = scratch[x][y0];
      CRGB b = scratch[x][y];
      CRGB c = scratch[x][y2];
      CRGB out;
      out.r = (uint8_t)((a.r + 2*b.r + c.r) / 4);
      out.g = (uint8_t)((a.g + 2*b.g + c.g) / 4);
      out.b = (uint8_t)((a.b + 2*b.b + c.b) / 4);
      *virtualLeds[x][y] = out;
    }
  }
}
