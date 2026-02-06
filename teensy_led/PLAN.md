# Frequency-Reactive Fire Visualization Plan

## Goal
Replace the current Music visualization pattern 9 (or add as new pattern 6) with a proper Fire2012-style fire effect where each strip's flames respond to its frequency band.

## Architecture

### Layout
- 10 strips for frequency bands (strips 1-10, bands 0-9)
- 2 outer strips (0 and 11) for beat flash
- 144 LEDs per strip (bottom = LED 0, top = LED 143)

### Core Algorithm: Fire2012 per-strip
Each strip maintains its own `heat[144]` array:

1. **Cooling**: All cells cool down, losing heat to air
   - `heat[y] -= random(0, cooling_factor)`
   - Higher positions cool faster (flames taper)

2. **Heat Diffusion**: Heat drifts upward
   - `heat[y] = (heat[y-1] + heat[y-2]*2) / 3`

3. **Audio-Driven Sparking**: Band amplitude ignites sparks at bottom
   - `spark_intensity = bandVis[band] * 255`
   - Ignite in bottom 10-15% of strip
   - More amplitude = more sparks + higher intensity

4. **Color Mapping**: Heat → Fire palette
   - 0 = black
   - 64 = dark red
   - 128 = orange
   - 192 = yellow
   - 255 = white

## Implementation Details

### Fire Palette (WLED-style)
```cpp
DEFINE_GRADIENT_PALETTE(fireAudio_gp) {
    0,   0,   0,   0,    // black
   32,  32,   0,   0,    // dark red
   64, 128,   0,   0,    // red
  128, 255,  80,   0,    // orange
  192, 255, 200,  30,    // yellow-orange
  240, 255, 255, 180,    // yellow-white
  255, 255, 255, 255     // white
};
```

### Tuning Parameters
```cpp
#define FIRE_COOLING     55    // How fast flames cool (higher = shorter flames)
#define FIRE_SPARKING   120    // Base spark probability
#define SPARK_ZONE       20    // Bottom N pixels where sparks can ignite
#define AUDIO_BOOST     2.5f   // Multiply band amplitude for spark intensity
```

### Per-Frame Update (per strip)
```cpp
void updateFireStrip(int strip, float bandLevel) {
    // 1. Cool down
    for (int y = 0; y < 144; y++) {
        uint8_t cooldown = random8(0, ((FIRE_COOLING * 10) / 144) + 2);
        heat[strip][y] = qsub8(heat[strip][y], cooldown);
    }

    // 2. Heat rises (diffuse upward)
    for (int y = 143; y >= 2; y--) {
        heat[strip][y] = (heat[strip][y-1] + heat[strip][y-2] + heat[strip][y-2]) / 3;
    }

    // 3. Audio-driven sparks at bottom
    uint8_t sparkIntensity = (uint8_t)(bandLevel * AUDIO_BOOST * 255);
    sparkIntensity = min(sparkIntensity, 255);

    if (random8() < FIRE_SPARKING + (sparkIntensity / 2)) {
        int y = random8(SPARK_ZONE);
        heat[strip][y] = qadd8(heat[strip][y], sparkIntensity);
    }

    // Extra sparks when band is loud
    if (bandLevel > 0.4f) {
        for (int i = 0; i < 3; i++) {
            int y = random8(SPARK_ZONE);
            heat[strip][y] = qadd8(heat[strip][y], random8(200, 255));
        }
    }

    // 4. Map heat to colors
    for (int y = 0; y < 144; y++) {
        *virtualLeds[strip][y] = ColorFromPalette(firePalette, heat[strip][y]);
    }
}
```

### Beat Enhancement
- On beat (`beatFlash > 0.5`):
  - Outer strips (0, 11) flash white
  - All strips get temporary cooling reduction (bigger flames)
  - Extra spark burst across all strips

## Files to Modify

### 1. main.cpp
- Add `renderEQFire()` function after existing renderers (~line 757)
- Add case 6 in `renderMusicVisualization()` switch
- Add fire palette definition
- Add static heat array: `static uint8_t fireHeat[NUM_VIRTUAL_STRIPS][LEDS_PER_VIRTUAL_STRIP]`

### 2. Globals.h (optional)
- Add fire tuning constants if we want them configurable

## Integration

Pattern selection in Music mode:
- 0: EQ Bars Basic
- 1: EQ Bars Rainbow
- 2: EQ Bars Center
- 3: EQ Peak Dots
- 4: EQ Pulse Columns
- 5: EQ Bars Mono
- **6: EQ Fire (NEW)** ← frequency-reactive fire

Uses 12 bands.

## Testing Checklist
- [ ] Fire looks realistic (smooth flames, proper colors)
- [ ] Each strip responds to its frequency band
- [ ] Bass (left) and treble (right) clearly distinguishable
- [ ] Beat flash works on outer strips
- [ ] No flicker or artifacts
- [ ] Smooth transitions when music changes
- [ ] Performance OK at 60fps
