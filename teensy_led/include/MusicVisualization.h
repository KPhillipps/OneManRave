#ifndef MUSIC_VISUALIZATION_H
#define MUSIC_VISUALIZATION_H

#include "Globals.h"

// ============================================================================
// Music Visualization Functions (FFT-reactive, called from main loop)
// ============================================================================

// Main entry point - computes bands and renders current visualization
void mapAmplitudesToLEDs();

// Individual visualizations (pattern index in Music mode)
// 0-6: EQ-style visualizations (rendered via renderMusicVisualization())
// 8: Fire2012 with audio enhancement
// 10: Meteorite Rain
// 11: Red Comet with Audio

void Fire2012WithAudioEnhanced();
void RedCometWithAudio1();
void meteoriteRain(bool reset = false);
void AuroraOrganic_Run(bool reset = false);
void AuroraNoteSparks_Run(bool reset = false);
void AuroraOnCloud_Run(bool reset = false);  // aurora blobs/sparks over cloud background
void setAuroraNote(uint8_t note, uint8_t velocity);   // optional, if FFT packets include a note

// Fire tuning parameters (persisted to EEPROM in main.cpp)
void setFireParams(float boost, uint8_t cooling, uint8_t sparking);
void getFireParams(float &boost, uint8_t &cooling, uint8_t &sparking);

#endif
