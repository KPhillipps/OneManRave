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

// NOTE: All P-mode patterns are non-reactive. No beat, no sound, no audio input.
// Use blocking loops with patternDelay(). Music reactivity belongs in M-mode only.
void runPattern() {
  static int lastPattern = -1;
  bool reset = (state.pattern != lastPattern);
  if (reset) {
    DBG_SERIAL_PRINTF("Running pattern %d\n", state.pattern);
    lastPattern = state.pattern;
  }
  switch (state.pattern) {
    case 0:
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

    case 1:
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

    case 2:
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

    case 3:
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


    case 4:
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
    case 5:
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

    case 6:
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
  float vx, vy;      // velocity (vy = smoothed bob offset)
  float vyDrift;     // vertical drift speed (px/sec)
  uint8_t rx, ry;    // radii in px
  uint16_t nseed;    // noise seed for edge wobble
  uint8_t hueJ;      // minor hue jitter
  float fade;        // 0..1 fade-in/out factor
  float life;        // seconds remaining before dissolution
  Layer layer;
  bool  alive;
  bool  dissolving;  // true = fading out, respawn when fade <= 0
};

static struct {
  uint32_t lastMs = 0;
  Zone zones[20];
  uint8_t zoneCount = ZONES_TOTAL;
  float xOffF=0, yOffF=0, tOffF=0; // float accumulators (cast to uint16_t for inoise8)
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

static void spawnOneCloud(Zone &q, Layer L) {
  q.layer = L;
  q.cx = random8(0, NUM_VIRTUAL_STRIPS-1) + 0.5f;
  q.cy = random16(20, LEDS_PER_VIRTUAL_STRIP - 20) + 0.5f;

  // ~70% smaller than previous, wide random range for varied shapes
  uint8_t rxMin=1, rxMax=2, ryMin=3, ryMax=10;
  if (L == FAR)  { rxMin=1; rxMax=1; ryMin=2; ryMax=8; }
  if (L == FORE) { rxMin=1; rxMax=3; ryMin=3; ryMax=12; }
  q.rx = random8(rxMin, rxMax+1);
  q.ry = random8(ryMin, ryMax+1);

  float base = BASE_DRIFT_PX_S;
  float f = (L==FAR)? FAR_FACTOR : (L==MID? MID_FACTOR : FORE_FACTOR);
  // Bidirectional horizontal drift
  float dir = (random8() & 1) ? 1.0f : -1.0f;
  q.vx = dir * (base * f) * (0.8f + (random8() / 255.0f) * 0.5f);
  q.vy = 0.0f;
  // Vertical drift: random up/down
  q.vyDrift = (base * f) * 3.0f * ((random8() & 1) ? 1.0f : -1.0f)
              * (0.8f + (random8() / 255.0f) * 0.4f);

  q.nseed = random16();
  q.hueJ  = random8();
  q.fade  = 0.0f;
  q.life  = 20.0f + (random8() / 255.0f) * 20.0f; // 20-40s lifetime
  q.alive = true;
  q.dissolving = false;
}

static void spawnClouds() {
  uint8_t z = 0;
  auto mk = [&](Layer L, uint8_t count){
    for (uint8_t i=0; i<count && z<cloudG.zoneCount; ++i,++z){
      spawnOneCloud(cloudG.zones[z], L);
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
  uint16_t tI = (uint16_t)cloudG.tOffF;
  uint16_t xI = (uint16_t)cloudG.xOffF;
  uint16_t yI = (uint16_t)cloudG.yOffF;
  for (int x=0; x<NUM_VIRTUAL_STRIPS; ++x){
    uint16_t xo = xI + x*123;
    for (int y=0; y<LEDS_PER_VIRTUAL_STRIP; ++y){
      uint8_t n = inoise8(xo, yI + y*37, tI);
      CRGB c = BackfieldColor(scale8(n, 70), (uint8_t)y);
      cloudP(x,y) = c;
    }
  }
}

static void updateZones(float dt) {
  // Float accumulators for smooth noise drift (no integer truncation)
  cloudG.tOffF += dt * 60.0f;          // ~1 per frame at 60fps
  cloudG.xOffF += dt * 120.0f;         // smooth background scroll
  cloudG.yOffF += dt * 60.0f;

  uint16_t tI = (uint16_t)cloudG.tOffF; // for noise lookups

  static constexpr float DISSOLVE_SEC = 3.0f; // fade-out duration

  for (uint8_t i=0;i<cloudG.zoneCount;i++){
    Zone &q = cloudG.zones[i];
    if (!q.alive) continue;

    // Drift
    q.cx += q.vx * dt;
    q.life -= dt;

    // Trigger dissolution: lifetime expired or drifted off-screen
    if (!q.dissolving) {
      bool offScreen = (q.cx < -(float)q.rx - 2) || (q.cx > NUM_VIRTUAL_STRIPS + q.rx + 2)
                     || (q.cy < -(float)q.ry) || (q.cy > LEDS_PER_VIRTUAL_STRIP + (float)q.ry);
      if (q.life <= 0.0f || offScreen) {
        q.dissolving = true;
      }
    }

    // Dissolving: fade out gradually, then respawn
    if (q.dissolving) {
      q.fade -= dt / DISSOLVE_SEC;
      if (q.fade <= 0.0f) {
        spawnOneCloud(q, q.layer);  // respawn as new cloud
        continue;
      }
    } else if (q.fade < 1.0f) {
      // Fade-in ramp
      q.fade += dt / FADE_IN_SEC;
      if (q.fade > 1.0f) q.fade = 1.0f;
    }

    // Vertical bob: noise-driven position offset for organic motion
    uint8_t n = inoise8((uint16_t)(q.nseed + tI * 2), (uint16_t)(q.nseed * 3));
    float bobTarget = ((int)n - 128) / 128.0f; // -1..1
    float bobOffset = bobTarget * 18.0f;        // ±18 pixels of gentle drift
    float cyBase = q.cy - q.vy;  // remove old bob to get base position
    cyBase += q.vyDrift * dt;    // vertical drift
    q.vy += (bobOffset - q.vy) * 0.02f;  // exponential smooth
    q.cy = cyBase + q.vy;
  }
}

static void drawZones() {
  for (uint8_t pass=0; pass<3; ++pass){
    for (uint8_t i=0;i<cloudG.zoneCount;i++){
      Zone &q = cloudG.zones[i];
      if (!q.alive || (uint8_t)q.layer != pass) continue;
      if (q.fade <= 0.0f) continue;

      int xmin = (int)floorf(q.cx - q.rx - 2);
      int xmax = (int)ceilf(q.cx + q.rx + 2);
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
            if (d2 > 5.0f) continue;

            // Soft Gaussian falloff
            float base = expf(-d2 * 0.25f);

            // Organic edges: two noise octaves, stronger further from center
            uint8_t wn1 = inoise8((uint16_t)(x*37 + q.nseed), (uint16_t)(y*29 + q.nseed), (uint16_t)cloudG.tOffF);
            uint8_t wn2 = inoise8((uint16_t)(x*73 + q.nseed*2), (uint16_t)(y*53 + q.nseed*2), (uint16_t)(cloudG.tOffF*1.5f));
            float edgeDist = sqrtf(d2); // 0 at center, grows outward
            float wobble = (((int)wn1 - 128) / 512.0f + ((int)wn2 - 128) / 1024.0f) * min(edgeDist, 1.5f);
            float alphaF = constrain(base + wobble, 0.0f, 1.0f);

            // Sub-pixel dithering for smoother horizontal movement
            float fracCx = q.cx - floorf(q.cx);
            float ditherMag = fracCx * (1.0f - fracCx) * 0.2f;
            int ditherSign = (((x * 3 + y * 7) ^ (int)(cloudG.tOffF * 0.5f)) & 1) ? 1 : -1;
            alphaF = constrain(alphaF + ditherMag * ditherSign, 0.0f, 1.0f);
            alphaF *= q.fade;
            uint8_t alpha = (uint8_t)(alphaF * 255.0f);

            // Lighter center: brighter and less saturated near core
            float centerBlend = expf(-d2 * 1.5f);
            uint8_t v = (q.layer==FORE) ? 195 : (q.layer==MID ? 182 : 168);
            uint8_t hue = 162 + (q.hueJ % 8);
            uint8_t sat = 110 + (q.hueJ % 20);
            sat = (uint8_t)max(0, (int)sat - (int)(centerBlend * 70));  // whiter center
            v   = (uint8_t)min(255, (int)v + (int)(centerBlend * 55)); // brighter center
            CRGB c = CHSV(hue, sat, v);
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
  if (dt > 0.10f) dt = 0.10f; // only clamp truly stale frames (100ms+), not normal jitter
  cloudG.lastMs = now;

  drawBackfield();
  updateZones(dt);
  drawZones();

  // Medium separable blur: horizontal radius 2 then vertical radius 1
  static CRGB scratch[NUM_VIRTUAL_STRIPS][LEDS_PER_VIRTUAL_STRIP];

  // Horizontal blur (wrap in X), radius 3, weights [1,2,3,4,3,2,1]/16
  for (int y = 0; y < LEDS_PER_VIRTUAL_STRIP; ++y) {
    for (int x = 0; x < NUM_VIRTUAL_STRIPS; ++x) {
      CRGB p[7];
      for (int k = -3; k <= 3; ++k)
        p[k+3] = *virtualLeds[(x + k + NUM_VIRTUAL_STRIPS) % NUM_VIRTUAL_STRIPS][y];
      scratch[x][y].r = (uint8_t)((p[0].r + 2*p[1].r + 3*p[2].r + 4*p[3].r + 3*p[4].r + 2*p[5].r + p[6].r) / 16);
      scratch[x][y].g = (uint8_t)((p[0].g + 2*p[1].g + 3*p[2].g + 4*p[3].g + 3*p[4].g + 2*p[5].g + p[6].g) / 16);
      scratch[x][y].b = (uint8_t)((p[0].b + 2*p[1].b + 3*p[2].b + 4*p[3].b + 3*p[4].b + 2*p[5].b + p[6].b) / 16);
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
