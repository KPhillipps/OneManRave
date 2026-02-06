// src/MusicAurora.cpp
#include <Arduino.h>
#include <math.h>
#include <FastLED.h>
#include "Globals.h"

// External symbols provided by your project
extern CRGB* virtualLeds[NUM_VIRTUAL_STRIPS][LEDS_PER_VIRTUAL_STRIP];
extern float bandAmplitude[];          // smoothed band magnitudes from FFT Teensy
extern int   currentBandCount;         // should be 12
extern uint8_t bandVis8[];             // 0-255 visual bands (AUX)
extern uint8_t bandDelta8[];           // 0-255 positive deltas (AUX)
extern uint8_t globalVis8;             // 0-255 overall energy
extern uint8_t peakDetected;           // 0/1 peak gate
extern unsigned long lastAuxPacketMs;  // timestamp of last AUX frame

// ---------- Palettes (cool blues) ----------
static const CRGBPalette16 kAurora = CRGBPalette16(
  CRGB(  2,  4, 20), CRGB(  3,  8, 35), CRGB(  5, 18, 60), CRGB(  0, 40, 90),
  CRGB(  0, 70,120), CRGB(  0,110,150), CRGB(  0,140,160), CRGB( 10,170,170),
  CRGB( 40,200,180), CRGB( 90,220,190), CRGB(160,235,210), CRGB(220,240,230),
  CRGB(255,255,235), CRGB(180,230,220), CRGB( 80,200,190), CRGB( 10,120,150)
);

// ---------- Tunables ----------
static constexpr float   AMP_SCALE        = 0.08f;   // bandAmplitude → 0..1
static constexpr float   ATTACK           = 0.35f;
static constexpr float   RELEASE          = 0.08f;
static constexpr float   BASE_GLOW        = 0.30f;   // background floor
static constexpr uint8_t BLEND_SOFT       = 90;      // background smear
static constexpr float   NOISE_SPEED      = 0.003f;  // background drift
static constexpr float   NOISE_SCALE      = 0.02f;   // background spatial scale (base, multiplied later)

// Blobs (dark waveform echoes)
static constexpr uint16_t MAX_BLOBS       = 220;
static constexpr uint8_t  BLOB_R          = 2;       // compact, thick streak
static constexpr uint16_t BLOB_LIFE_MIN   = 320;
static constexpr uint16_t BLOB_LIFE_MAX   = 520;
static constexpr float    BLOB_GRAV_MIN   = 0.02f;
static constexpr float    BLOB_GRAV_MAX   = 0.05f;
static constexpr float    BLOB_DRIFT      = 0.15f;
static constexpr uint8_t  BLOB_DIM        = 200;     // stronger darkening for contrast
static constexpr int      BLOB_BOTTOM_DISPERSION_Y = 30;

// Sparkles (vocal peaks)
static constexpr uint16_t MAX_SPARKS      = 120;
static constexpr uint8_t  SPARK_LIFE_MIN  = 10;
static constexpr uint8_t  SPARK_LIFE_MAX  = 30;
static constexpr float    SPARK_SPEED_MIN = 0.6f;
static constexpr float    SPARK_SPEED_MAX = 1.5f;
static constexpr uint8_t  SPARK_TRAIL     = 180;
static constexpr uint8_t  VOCAL_LOW_BAND  = 3;
static constexpr uint8_t  VOCAL_HIGH_BAND = 7;

// Optional note input (kept for compatibility)
static uint8_t gNote = 255;
static uint8_t gNoteVel = 0;
void setAuroraNote(uint8_t note, uint8_t velocity) { gNote = note; gNoteVel = velocity; }

// Utilities
static inline CRGB& P(int x, int y) {
  if (x < 0) x = 0;
  else if (x >= NUM_VIRTUAL_STRIPS) x = NUM_VIRTUAL_STRIPS - 1;

  if (y < 0) y = 0;
  else if (y >= LEDS_PER_VIRTUAL_STRIP) y = LEDS_PER_VIRTUAL_STRIP - 1;

  return *virtualLeds[x][y];
}
static inline float frand() { return random(0, 10000) / 10000.0f; }
static inline int bandToX(int band, int bands) {
  if (bands < 2) return 0;
  return (int)lroundf(((band + 0.5f) * (NUM_VIRTUAL_STRIPS - 1)) / bands);
}

// State
struct BlobParticle { float x,y,vx,vy; uint16_t life; uint8_t val; bool active; };
struct SparkParticle { float x,y,vx,vy; uint8_t life; uint8_t hue; bool active; };
struct AState {
  uint32_t lastMs = 0;
  float energy[12] = {0};
  float prevBand[12] = {0};
  uint8_t peakCD[12] = {0};
  bool peakFlag[12] = {0};
  BlobParticle blobs[MAX_BLOBS];
  SparkParticle sparks[MAX_SPARKS];
  uint32_t noiseStartMs = 0;   // millis() origin for background noise
  void reset() { *this = AState(); }
};
static AState S;

// Background: dim cyan/blue with noise-driven hue/brightness
static void renderBackground() {
  // Use millis-based Z to avoid short-period wrap (>> keeps drift very slow)
  if (S.noiseStartMs == 0) S.noiseStartMs = millis();
  uint32_t tMs = millis() - S.noiseStartMs;
  uint16_t z = (uint16_t)(tMs >> 3); // slower drift; increase shift for even slower

  for (int x = 0; x < NUM_VIRTUAL_STRIPS; x++) {
    for (int y = 0; y < LEDS_PER_VIRTUAL_STRIP; y++) {
      uint16_t nx = (uint16_t)(x * 65535.0f * NOISE_SCALE * 3.5f); // finer spatial detail
      uint16_t ny = (uint16_t)(y * 65535.0f * NOISE_SCALE * 3.5f);
      uint8_t n = inoise8(nx, ny, z);
      uint8_t hueShift = scale8(n, 6);                   // small hue drift
      // Lower floor and swing for darker canvas
      uint8_t baseV = 70 + scale8(n, 30);                // ~70..100
      CRGB c = ColorFromPalette(kAurora, qadd8(n, hueShift), baseV, LINEARBLEND);
      P(x, y) = c;                                       // overwrite each frame
    }
  }
}

// Peak detection helper using AUX bandDelta if fresh; fallback to rise on bandAmplitude
static bool bandIsPeak(int b, float energy) {
  bool auxFresh = (lastAuxPacketMs != 0) && (millis() - lastAuxPacketMs < 200);
  if (auxFresh) {
    return bandDelta8[b] > 38; // empirical threshold
  } else {
    float rise = energy - S.prevBand[b];
    return (rise > 0.08f) && (energy > 0.18f);
  }
}

// Spawn blobs on peaks (multiple for thickness)
static void spawnBlobs(int band, float energy, int bands) {
  int base = 3;
  int extra = (int)(energy * 10.0f);          // more energy → more blobs
  int toSpawn = base + extra + random8(0, 3); // 3 + energy*10 (+0..2)
  if (toSpawn > 14) toSpawn = 14;             // cap
  int col = bandToX(band, bands);
  for (int i = 0; i < toSpawn; i++) {
    // find free slot
    BlobParticle* p = nullptr;
    for (auto &bp : S.blobs) { if (!bp.active) { p = &bp; break; } }
    if (!p) break;
    p->active = true;
    p->x = col + (frand() - 0.5f) * 0.5f;
    p->y = LEDS_PER_VIRTUAL_STRIP - 1 - random8(0, 8);   // spawn top 8 px
    p->vx = (frand() - 0.5f) * BLOB_DRIFT * 2.0f;
    p->vy = -0.2f;  // downward (row decreases)
    p->life = random(BLOB_LIFE_MIN, BLOB_LIFE_MAX);
    // darker on stronger peaks (stronger contrast)
    uint8_t val = (uint8_t)constrain(200 - (int)(energy * 180.0f), 30, 230);
    p->val = val;
  }
}

// Spawn sparkles on vocal peaks (bands 3–7) in upper/center area
static void spawnSparkles() {
  bool auxFresh = (lastAuxPacketMs != 0) && (millis() - lastAuxPacketMs < 200);
  // derive vocal peak gate strictly from peaks to keep sync
  bool vocalPeak = false;
  for (int b = VOCAL_LOW_BAND; b <= VOCAL_HIGH_BAND; b++) {
    if (S.peakFlag[b] || (auxFresh && bandDelta8[b] > 45)) { vocalPeak = true; break; }
  }
  if (!vocalPeak && !peakDetected) return;

  int count = 1 + random8(0, 4); // a few sparks per peak
  for (int i = 0; i < count; i++) {
    SparkParticle* s = nullptr;
    for (auto &sp : S.sparks) { if (!sp.active) { s = &sp; break; } }
    if (!s) break;
    s->active = true;
    s->x = 3.5f + frand() * 5.0f;                     // columns ~3–8
    s->y = 60.0f + frand() * 84.0f;                   // upper 60%
    float ang = (frand() - 0.5f) * PI / 3.0f;         // mostly vertical
    float spd = SPARK_SPEED_MIN + frand() * (SPARK_SPEED_MAX - SPARK_SPEED_MIN);
    s->vx = cosf(ang) * spd * 0.3f;
    s->vy = -fabsf(sinf(ang) * spd);                  // slight upward or downward; negative = down
    s->life = random(SPARK_LIFE_MIN, SPARK_LIFE_MAX);
    s->hue = 128 + random8(32);                       // cyan/white bias
  }
}

static void stepBlobs() {
  for (auto &b : S.blobs) if (b.active) {
    float grav = BLOB_GRAV_MIN + frand() * (BLOB_GRAV_MAX - BLOB_GRAV_MIN);
    b.vy -= grav;          // more negative -> faster downward
    b.x  += b.vx;
    b.y  += b.vy;
    b.vx += (frand() - 0.5f) * 0.02f; // slight drift wiggle
    b.vx *= 0.98f;

    // bottom dispersion
    if (b.y < BLOB_BOTTOM_DISPERSION_Y) {
      b.vx += (frand() - 0.5f) * 0.06f;
      b.val = qadd8(b.val, 5);
      if (b.life > 5) b.life -= 5;
    }

    if (b.y < 0 || b.x < -2 || b.x > NUM_VIRTUAL_STRIPS + 2 || b.life == 0) {
      b.active = false;
    } else {
      b.life--;
    }
  }
}

static void renderBlobs() {
  for (auto &b : S.blobs) if (b.active) {
    int cx = (int)lroundf(b.x);
    int cy = (int)lroundf(b.y);
    for (int dx = -BLOB_R; dx <= BLOB_R; dx++) {
      for (int dy = -BLOB_R; dy <= BLOB_R; dy++) {
        int xx = cx + dx, yy = cy + dy;
        if (xx < 0 || xx >= NUM_VIRTUAL_STRIPS || yy < 0 || yy >= LEDS_PER_VIRTUAL_STRIP) continue;
        if (dx*dx + dy*dy > BLOB_R*BLOB_R) continue;
        // darken locally but keep coherence by taking per-channel min
        CRGB shadow = CHSV(160, 255, b.val);
        CRGB &dst = P(xx, yy);
        dst.r = min(dst.r, shadow.r);
        dst.g = min(dst.g, shadow.g);
        dst.b = min(dst.b, shadow.b);
      }
    }
  }
}

static void stepSparks() {
  for (auto &s : S.sparks) if (s.active) {
    s.x += s.vx;
    s.y += s.vy;
    s.vx *= 0.96f;
    s.vy *= 0.92f;
    if (s.life == 0 || s.x < -2 || s.x > NUM_VIRTUAL_STRIPS + 2 || s.y < 0 || s.y >= LEDS_PER_VIRTUAL_STRIP) {
      s.active = false;
    } else {
      s.life--;
    }
  }
}

static void renderSparks() {
  for (auto &s : S.sparks) if (s.active) {
    int x = (int)lroundf(s.x);
    int y = (int)lroundf(s.y);
    if (x < 0 || x >= NUM_VIRTUAL_STRIPS || y < 0 || y >= LEDS_PER_VIRTUAL_STRIP) continue;
    CRGB c = CHSV(s.hue, 80, 255);     // bright cyan/white
    P(x, y) += c;                      // additive
    if (x > 0) P(x-1, y).fadeToBlackBy(255 - SPARK_TRAIL), P(x-1, y) += c;
    if (x < NUM_VIRTUAL_STRIPS-1) P(x+1, y).fadeToBlackBy(255 - SPARK_TRAIL), P(x+1, y) += c;
    if (y < LEDS_PER_VIRTUAL_STRIP-1) P(x, y+1).fadeToBlackBy(255 - SPARK_TRAIL), P(x, y+1) += c;
  }
}

// ------------------------------------------------------------------
// Public entry points
// ------------------------------------------------------------------
void AuroraOrganic_Run(bool reset) {
  if (reset) S.reset();
  int bands = currentBandCount > 0 ? currentBandCount : 12;

  // Smooth energy and detect peaks per band
  bool auxFresh = (lastAuxPacketMs != 0) && (millis() - lastAuxPacketMs < 200);
  for (int b = 0; b < 12; b++) {
    float raw = (b < bands) ? bandAmplitude[b] : 0.0f;
    float t = raw * AMP_SCALE;
    if (t > 1.0f) t = 1.0f;
    if (t < 0.0f) t = 0.0f;
    float prev = S.energy[b];
    float a = (t > prev) ? ATTACK : RELEASE;
    S.energy[b] = prev + (t - prev) * a;

    if (S.peakCD[b] > 0) S.peakCD[b]--;
    bool isPeak = bandIsPeak(b, S.energy[b]) && (S.peakCD[b] == 0);
    S.peakFlag[b] = isPeak;
    if (isPeak) {
      S.peakCD[b] = 5;  // small refractory
      spawnBlobs(b, S.energy[b], bands);
    }
    S.prevBand[b] = S.energy[b];
  }

  spawnSparkles();      // vocal-driven
  renderBackground();   // living field (full overwrite to avoid periodic fade)
  stepBlobs();
  renderBlobs();        // dark silhouettes
  stepSparks();
  renderSparks();       // additive overlay
  FastLED.show();
}

void AuroraNoteSparks_Run(bool reset) {
  // Reuse same behavior; palette already cool. Keeping separate entry for pattern map.
  AuroraOrganic_Run(reset);
}
