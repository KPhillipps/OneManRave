#ifndef MUSIC_VISUALIZATION_H
#define MUSIC_VISUALIZATION_H

#include <Audio.h>
#include "Globals.h"

// Core music visualization functions
void runGraphicEQ();
void calculateBandAmplitudes();
void mapAmplitudesToLEDs();
void computeVerticalLevels();
void detectAndVisualizeBeat();


// Audio setup
void setupAudio();

#endif
