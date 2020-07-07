#include <FastLED.h>

// How many leds in your strip?
#define NUM_LEDS 288
#define ACTUAL_LEDS 64

// For led chips like Neopixels, which have a data line, ground, and power, you just
// need to define DATA_PIN.  For led chipsets that are SPI based (four wires - data, clock,
// ground, and power), like the LPD8806, define both DATA_PIN and CLOCK_PIN
#define DATA_PIN 22
#define CLOCK_PIN 23

// Define the array of leds
CRGB leds[NUM_LEDS];

int loops = 0;
unsigned long begining = millis();

void setup() {
  Serial.begin(115200);
  Serial.println("resetting");
  LEDS.addLeds<APA102, DATA_PIN, CLOCK_PIN, RGB,DATA_RATE_MHZ(24)>(leds, NUM_LEDS);
  LEDS.setBrightness(84);
}

void fadeall() {
  for (int i = 0; i < ACTUAL_LEDS; i++) {
    leds[i].nscale8(250);
  }
}

void loop() {


  loops++;
  int howlong = millis() - begining;

  if ((millis() - begining ) >= 1000 ) {
    Serial.printf("refreshed %d times in %d milliseconds\n", loops *2, millis() - begining);
    begining = millis();
    loops = 0;
  }

  static uint8_t hue = 0;
  // First slide the led in one direction
  for (int i = 0; i < ACTUAL_LEDS; i++) {
    // Set the i'th led to red
    leds[i] = CHSV(hue++, 255, 255);
    // Show the leds
    FastLED.show();
    // now that we've shown the leds, reset the i'th led to black
    // leds[i] = CRGB::Black;
    fadeall();
    // Wait a little bit before we loop around and do it again
    	//delay(1);
  //   delayMicroseconds(300);
  }

  // Now go in the other direction.
  for (int i = (ACTUAL_LEDS) - 1; i >= 0; i--) {
    // Set the i'th led to red
    leds[i] = CHSV(hue++, 255, 255);
    // Show the leds
    FastLED.show();
    // now that we've shown the leds, reset the i'th led to black
    // leds[i] = CRGB::Black;
    fadeall();
    // Wait a little bit before we loop around and do it again
    	//delay(1);
   //  delayMicroseconds(300);

  }


}
