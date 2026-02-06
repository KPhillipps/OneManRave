#include "Globals.h"
#include "ColorDefinitions.h"



void meteorShower(CRGB color, int size, int decay) {
    for (int strip = 0; strip < NUM_VIRTUAL_STRIPS; strip++) {
        for (int start = LEDS_PER_VIRTUAL_STRIP - 1; start >= -size; start--) {
            for (int led = 0; led < size; led++) {
              if (Serial1.available()) { return; }
                int index = start + led;
                if (index >= 0 && index < LEDS_PER_VIRTUAL_STRIP) {
                    virtualLeds[strip][index] = color; // Set the meteor's color
                }
            }
            FastLED.show();
            // Fade all LEDs for a trailing effect
            for (int led = 0; led < LEDS_PER_VIRTUAL_STRIP; led++) {
                virtualLeds[strip][led].fadeToBlackBy(decay);
            }
        }
    }
}

void bouncingBallEffect(bool reset = false) {
    static float position = 0;     // Current position of the ball
    static float velocity = 0;     // Velocity of the ball
    const float gravity = 0.5;     // Acceleration due to gravity
    const int totalHeight = NUM_VIRTUAL_STRIPS * LEDS_PER_VIRTUAL_STRIP; // Total height of all strips
    const int bottomPosition = 0;  // Bottom of the virtual column (first LED)
    const int topPosition = totalHeight - 1; // Top of the virtual column (last LED)

    // Reset logic
    if (reset) {
        position = topPosition;  // Reset to the top of the virtual column
        velocity = -1;           // Reset velocity with a small downward motion
    }

    // Update velocity and position
    velocity += gravity;         // Gravity pulls the ball downward
    position += velocity;        // Update position based on velocity

    // Debug: Print position and velocity
    Serial.printf("Position: %.2f, Velocity: %.2f\n", position, velocity);

    // Check for bounce at the bottom
    if (position <= bottomPosition) {
        position = bottomPosition;  // Clamp to the bottom
        velocity = abs(velocity) * 0.8; // Reverse direction with energy loss
        Serial.println("Bounce at bottom!");
    }

    // Check for bounce at the top
    if (position > topPosition) {
        position = topPosition;    // Clamp to the top
        velocity = -abs(velocity); // Ensure the velocity is downward
        Serial.println("Bounce at top!");
    }

    // Clear all LEDs
    for (int strip = 0; strip < NUM_VIRTUAL_STRIPS; strip++) {
        for (int led = 0; led < LEDS_PER_VIRTUAL_STRIP; led++) {
            virtualLeds[strip][led] = CRGB::Black;
        }
    }

    // Calculate the current strip and LED position
    int currentPosition = static_cast<int>(position); // Convert position to integer
    int currentStrip = currentPosition / LEDS_PER_VIRTUAL_STRIP; // Determine strip index
    int currentLED = LEDS_PER_VIRTUAL_STRIP - 1 - (currentPosition % LEDS_PER_VIRTUAL_STRIP); // Invert LED index

    // Light the current LED
    if (currentStrip >= 0 && currentStrip < NUM_VIRTUAL_STRIPS && currentLED >= 0) {
        virtualLeds[currentStrip][currentLED] = CHSV(128, 255, 255); // Set ball color
    }

    FastLED.show();
    delay(20); // Adjust animation speed
}





void runPattern() {
  static int lastPattern = -1;
  if (state.pattern != lastPattern) {
    Serial.printf("Running pattern %d\n", state.pattern);
    lastPattern = state.pattern;
  }
  switch (state.pattern) {
    case 0:
      break;

    case 1:
      break;

    case 2:
      {                          // Rainbow Pattern
        static uint8_t hue = 0;  // Global hue, incremented over time
        uint8_t deltaHue = 7;    // Default spacing for rainbow effect

        for (;;) {
          if (Serial1.available()) {
            break;  // Exit loop on state change
          }

          // Fill virtualLeds with a rainbow pattern
          for (int strip = 0; strip < NUM_VIRTUAL_STRIPS; strip++) {
            for (int led = 0; led < LEDS_PER_VIRTUAL_STRIP; led++) {
              virtualLeds[strip][led] = CHSV(hue + (strip * LEDS_PER_VIRTUAL_STRIP + led) * deltaHue, 255, 255);
            }
          }

          FastLED.show();
          delay(20);  // Animation speed
          hue++;      // Increment hue for the next frame
        }
      }
      break;

    case 3:
      {  // Rainbow with Sparkle
        static uint8_t hue = 0;
        uint8_t deltaHue = 7;
        uint8_t sparkleChance = 100;

        for (;;) {
          if (Serial1.available()) {
            break;
          }

          // Fill virtualLeds with a rainbow pattern
          for (int strip = 0; strip < NUM_VIRTUAL_STRIPS; strip++) {
            for (int led = 0; led < LEDS_PER_VIRTUAL_STRIP; led++) {
              virtualLeds[strip][led] = CHSV(hue + (strip * LEDS_PER_VIRTUAL_STRIP + led) * deltaHue, 255, 255);
            }
          }

          // Add a sparkle effect
          if (random8() < sparkleChance) {
            int randomStrip = random(NUM_VIRTUAL_STRIPS);
            int randomLed = random(LEDS_PER_VIRTUAL_STRIP);
            virtualLeds[randomStrip][randomLed] = CRGB::White;  // Flash white
          }

          FastLED.show();
          delay(20);
          hue++;
        }
      }
      break;

    case 4:
      {  // Fire Pattern
#define COOLING 100
#define SPARKING 180

        static uint8_t heat[NUM_VIRTUAL_STRIPS][LEDS_PER_VIRTUAL_STRIP] = { { 0 } };

        for (;;) {
          if (Serial1.available()) {
            break;
          }

          for (int strip = 0; strip < NUM_VIRTUAL_STRIPS; strip++) {
            // Step 1: Cool down the heat
            for (int y = 0; y < LEDS_PER_VIRTUAL_STRIP; y++) {
              heat[strip][y] = qsub8(heat[strip][y], random8(0, ((COOLING * 10) / LEDS_PER_VIRTUAL_STRIP) + 2));
            }

            // Step 2: Heat diffuses upward
            for (int y = LEDS_PER_VIRTUAL_STRIP - 1; y >= 2; y--) {
              heat[strip][y] = (heat[strip][y - 1] + heat[strip][y - 2] + heat[strip][y - 2]) / 3;
            }

            // Step 3: Ignite new sparks at the bottom
            if (random8() < SPARKING) {
              int y = random8(7);
              heat[strip][y] = qadd8(heat[strip][y], random8(160, 255));
            }

            // Step 4: Map heat to colors
            for (int y = 0; y < LEDS_PER_VIRTUAL_STRIP; y++) {
              virtualLeds[strip][y] = HeatColor(heat[strip][y]);
            }
          }

          FastLED.show();
          delay(5);
        }
      }
      break;

    case 5:
      {  // Sinelon Pattern
        static uint16_t position[NUM_VIRTUAL_STRIPS] = { 0 };
        static uint8_t hue[NUM_VIRTUAL_STRIPS] = { 0 };

        for (;;) {
          if (Serial1.available()) {
            break;  // Exit on Serial1 input
          }

          // Clear all LEDs
          for (int strip = 0; strip < NUM_VIRTUAL_STRIPS; strip++) {
            for (int led = 0; led < LEDS_PER_VIRTUAL_STRIP; led++) {
              virtualLeds[strip][led] = CRGB::Black;  // Turn off all LEDs
            }
          }

          // Update each strip with the Sinelon effect
          for (int strip = 0; strip < NUM_VIRTUAL_STRIPS; strip++) {
            hue[strip] += 8;                                                             // Increment hue for color variation
            position[strip] = beatsin16(13 + strip * 2, 0, LEDS_PER_VIRTUAL_STRIP - 1);  // Smooth sinusoidal motion

            // Highlight the current position on the strip
            virtualLeds[strip][position[strip]] = CHSV(hue[strip], 255, 255);

            // Add trailing fade effect
            for (int led = 0; led < LEDS_PER_VIRTUAL_STRIP; led++) {
              if (led != position[strip]) {
                virtualLeds[strip][led].fadeToBlackBy(60);  // Dim non-highlighted LEDs
              }
            }
          }

          FastLED.show();  // Update LEDs
          delay(15);       // Adjust for animation speed
        }
      }
      break;


    case 6:
      {  // Enhanced Sinelon Pattern
        static uint16_t position[NUM_VIRTUAL_STRIPS] = { 0 };
        static uint8_t hue[NUM_VIRTUAL_STRIPS] = { 0 };
        const int cometLength = 20;

        for (;;) {
          if (Serial1.available()) {
            break;
          }

          // Clear LEDs
          FastLED.clear();

          // Update each strip
          for (int strip = 0; strip < NUM_VIRTUAL_STRIPS; strip++) {
            hue[strip] += 8;
            position[strip] = beatsin16(13 + strip * 2, 0, LEDS_PER_VIRTUAL_STRIP - 1);

            // Create comet
            for (int offset = 0; offset < cometLength; offset++) {
              int trailPosition = (position[strip] - offset + LEDS_PER_VIRTUAL_STRIP) % LEDS_PER_VIRTUAL_STRIP;
              uint8_t brightness = 255 - (offset * (255 / cometLength));
              virtualLeds[strip][trailPosition] += CHSV(hue[strip], 255, brightness);
            }
          }

          FastLED.show();
          delay(15);
        }
      }
      break;
    case 7:
      FastLED.clear();
      FastLED.show();
        for (;;) {
          if (Serial1.available()) {
            break;  // Exit loop on state change
          }

        meteorShower(CRGB::Red, 5,50); // Red meteor with size 5 and decay of 60
    
        meteorShower(CRGB::Blue, 8, 80); // Blue meteor with size 8 and decay of 80
  
      }
      break;
case 8:
      FastLED.clear();
      FastLED.show();
    // Reset the ball's position and velocity before starting
    bouncingBallEffect(true); // Pass `true` to reset

    for (;;) { // Infinite loop to keep the effect running
        if (Serial1.available()) {
            break; // Exit the loop on new serial input
        }

        bouncingBallEffect(); // Continuously animate the bouncing ball
    }
    break;

    case 9: // Fire with Audio Enhancement
      {
        #define FIRE_COOLING 150
        #define FIRE_SPARKING 80
        static uint8_t heat[NUM_VIRTUAL_STRIPS][LEDS_PER_VIRTUAL_STRIP];
        
        for (;;) {
          if (Serial1.available()) {
            break;
          }
          
          for (int strip = 0; strip < NUM_VIRTUAL_STRIPS; strip++) {
            // Cool down cells
            for (int i = 0; i < LEDS_PER_VIRTUAL_STRIP; i++) {
              heat[strip][i] = qsub8(heat[strip][i], random8(0, ((FIRE_COOLING * 5) / LEDS_PER_VIRTUAL_STRIP) + 2));
            }
            
            // Heat drifts upward
            for (int k = LEDS_PER_VIRTUAL_STRIP - 1; k >= 2; k--) {
              heat[strip][k] = (heat[strip][k - 1] + heat[strip][k - 2]) / 2;
            }
            
            // Random sparks
            if (random8() < FIRE_SPARKING) {
              int y = random8(7);
              heat[strip][y] = qadd8(heat[strip][y], random8(160, 255));
            }
            
            // Audio-based sparks (strips 1-10 mapped to bands 0-9)
            if (strip >= 1 && strip < NUM_VIRTUAL_STRIPS - 1) {
              int band = strip - 1;
              float audioLevel = bandAmplitude[band];
              
              if (audioLevel > 0.01) {
                int sparkY = random8(LEDS_PER_VIRTUAL_STRIP / 4); // Spark in bottom quarter
                uint8_t sparkIntensity = (uint8_t)(audioLevel * 255);
                heat[strip][sparkY] = qadd8(heat[strip][sparkY], sparkIntensity);
              }
            }
            
            // Map heat to LED colors with flicker
            for (int j = 0; j < LEDS_PER_VIRTUAL_STRIP; j++) {
              CRGB color = HeatColor(heat[strip][j]);
              color.nscale8_video(128 + random8(128)); // Flicker effect
              virtualLeds[strip][j] = color;
            }
          }
          
          FastLED.show();
          delay(15);
        }
      }
      break;

    case 10: // Red Comet with Audio
      {
        #define COMET_COOLING 150
        static uint8_t heat[NUM_VIRTUAL_STRIPS][LEDS_PER_VIRTUAL_STRIP];
        
        for (;;) {
          if (Serial1.available()) {
            break;
          }
          
          for (int strip = 0; strip < NUM_VIRTUAL_STRIPS; strip++) {
            // Cool down cells with increasing intensity as we go up
            for (int i = 0; i < LEDS_PER_VIRTUAL_STRIP; i++) {
              uint8_t coolingFactor = ((COMET_COOLING * (i + 1)) / LEDS_PER_VIRTUAL_STRIP) + 2;
              heat[strip][i] = qsub8(heat[strip][i], random8(0, coolingFactor));
            }
            
            // Audio-based comet trigger (strips 1-10 mapped to bands 0-9)
            if (strip >= 1 && strip < NUM_VIRTUAL_STRIPS - 1) {
              int band = strip - 1;
              float audioLevel = bandAmplitude[band];
              
              if (audioLevel > 0.01) {
                int cometY = random8(LEDS_PER_VIRTUAL_STRIP / 4); // Start in bottom quarter
                uint8_t cometIntensity = (uint8_t)(audioLevel * 255);
                heat[strip][cometY] = qadd8(heat[strip][cometY], cometIntensity);
              }
            }
            
            // Propagate the comet upward
            for (int k = LEDS_PER_VIRTUAL_STRIP - 1; k > 2; k--) {
              heat[strip][k] = (heat[strip][k - 1] * 3 + heat[strip][k - 2] * 2 + heat[strip][k - 3]) / 6;
            }
            
            // Map heat to flame colors with deeper red transition
            for (int j = 0; j < LEDS_PER_VIRTUAL_STRIP; j++) {
              CRGB color = HeatColor(heat[strip][j]);
              
              // Shift toward deeper red as we move up
              uint8_t redIntensity = scale8(color.r, 255 - (j * 15));
              uint8_t greenIntensity = scale8(color.g, 255 - (j * 25));
              uint8_t blueIntensity = scale8(color.b, 128 - (j * 10));
              
              virtualLeds[strip][j] = CRGB(redIntensity, greenIntensity, blueIntensity);
            }
          }
          
          FastLED.show();
          delay(15);
        }
      }
      break;

    default:
      break;
  }
}
