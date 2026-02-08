#ifndef MUSIC_VISUALIZATION_H
#define MUSIC_VISUALIZATION_H

#include <Audio.h>

// Core music visualization functions
void runGraphicEQ();
void calculateBandAmplitudes();
void mapAmplitudesToLEDs();
void Fire2012WithAudioEnhanced();
void RedCometWithAudio1();
void meteoriteRain(bool reset = false);
void vocalAurora(bool reset = false);
void harmonicAurora(bool reset = false);
void RedCometWithAudio();
void detectBeat();
void computeVerticalLevels();
void setFireParams(float boost, uint8_t cooling, uint8_t sparking);
void getFireParams(float &boost, uint8_t &cooling, uint8_t &sparking);

// Audio setup
void setupAudio();

#endif
