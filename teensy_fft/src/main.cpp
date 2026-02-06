#include <Arduino.h>
#include <cstring>
#include <cmath>
extern "C" char usb_string_serial_number[];
#include <Audio.h>

// ============================================================================
// TEENSY FFT (LEFT CHANNEL) - Audio Analysis & Command Bridge
// ============================================================================
// DUAL CHANNEL SYSTEM ARCHITECTURE - LEFT SIDE:
//   [ESP32 Master] --Serial--> [THIS TEENSY FFT] --Serial--> [LED Controller Left]
//         |                          |
//         |                    [SPDIF Input Pin 15]
//         |                          |
//     (Commands + Beat)         (LEFT channel audio)
//
// THIS DEVICE (LEFT Channel FFT Teensy) - THE AUDIO BRAIN:
//   ROLE: Audio analysis + command forwarding for LEFT channel
//
//   INPUTS:
//     1. Audio: LEFT channel audio via SPDIF (Pin 15)
//     2. Commands: From ESP32 via Serial1 RX Pin 0 @ 38400 baud
//
//   OUTPUTS:
//     1. FFT Data + Vocal Envelope/Flags to LED Controller via Serial2 TX Pin 8 @ 460800 baud
//     2. AUX Audio Features (bandVis, deltas, peak Hz, flux) via Serial2 TX Pin 8 @ 460800 baud
//
//   OPERATION:
//     1. Continuously analyzes LEFT audio channel using 1024-point FFT
//     2. Groups FFT bins into 10 frequency bands (low to high)
//     3. Smooths and normalizes band amplitudes
//     4. Computes vocal envelope + syllable hit + pitch class
//     5. Transmits FFT + vocal data packets to LED Controller @ 60 FPS
//
// BEAT DETECTION:
//   - Disabled. This device now sends vocal envelope + syllable flags instead.
//
// ============================================================================

// Audio setup - SPDIF Input (Pin 15)
AudioInputSPDIF3 spdifInput;
AudioAnalyzeFFT1024 fftLeft;
AudioAnalyzeFFT1024 fftRight;
AudioConnection patchCordLeft(spdifInput, 0, fftLeft, 0);   // Channel 0 (Left)
AudioConnection patchCordRight(spdifInput, 1, fftRight, 0); // Channel 1 (Right)

// FFT band grouping - 12 bands (patterns 0-5)
// More resolution in mids, band 11 is 8.5-20kHz
const int BANDS_12 = 12;
const int binGroups12[12][2] = {
    {1, 1},       // ~43 Hz      Sub-bass
    {2, 2},       // ~86 Hz      Bass
    {3, 4},       // 129-172 Hz  Upper bass
    {5, 7},       // 215-301 Hz  Low mids
    {8, 20},      // 344-860 Hz  Mids low
    {21, 35},     // 903-1.5k    Mids high
    {36, 55},     // 1.5-2.4k    Upper mids
    {56, 80},     // 2.4-3.4k    Presence low
    {81, 115},    // 3.5-5.0k    Presence high
    {116, 155},   // 5.0-6.7k    Brilliance
    {156, 196},   // 6.7-8.5k    High
    {197, 464}    // 8.5-20k     Air
};

// FFT band grouping - 10 bands (patterns 6+)
const int BANDS_10 = 10;
const int binGroups10[10][2] = {
    {1, 1}, {2, 2}, {3, 4}, {5, 7}, {8, 15},
    {16, 29}, {30, 58}, {59, 116}, {117, 232}, {233, 464}
};

// Maximum bands (for array sizing)
const int MAX_BANDS = 12;

// Current band configuration
int activeBands = 12;  // Default to 12 bands
int currentPattern = 0;

// Display settings
const float smoothingFactor = 0.15;
const float FFT_CAL_GAIN = 8000.0f;
const float bandTilt12[BANDS_12] = {
    1.0f, 1.0f, 1.05f, 1.1f, 1.15f, 1.2f,
    1.3f, 1.4f, 1.5f, 1.7f, 1.85f, 2.0f};
const float bandTilt10[BANDS_10] = {
    1.0f, 1.0f, 1.05f, 1.1f, 1.2f,
    1.3f, 1.4f, 1.6f, 1.8f, 2.0f};

// FFT bin frequency (Hz per bin at 44.1k / 1024)
const float BIN_FREQ_HZ = 44100.0f / 1024.0f;
const int PEAK_BIN_START = 2;    // ~86 Hz
const int PEAK_BIN_END = 255;    // ~11 kHz
const float PEAK_MAG_LOG_K = 60.0f;

// Visual band scaling (mirrors LED-side computeVisualBands)
const float VIS_SCALE = 0.005f;
const float VIS_LOG_K = 15.0f;
const float VIS_DELTA_GAIN = 4.0f;   // Boost transient deltas
const float VIS_FLUX_GAIN = 3.0f;    // Boost summed deltas
const float PEAK_AVG_ALPHA = 0.05f;  // Moving average for peak detection
const float PEAK_DETECT_RATIO = 1.6f;
const float PEAK_DETECT_MIN = 0.05f;

static const float bandGainVis[BANDS_12] = {
    1.00f, 1.00f, 1.05f, 1.10f, 1.10f, 1.15f,
    1.25f, 1.35f, 1.50f, 1.70f, 1.90f, 2.10f
};

// FFT state (sized for max bands)
float smoothedBandAmplitude[MAX_BANDS] = {0};
float lastCalibratedEnergy[MAX_BANDS] = {0};

// FFT-derived visual metrics (sent to LED as AUX frame)
static float bandVisFft[MAX_BANDS] = {0.0f};
static uint8_t bandVis8[MAX_BANDS] = {0};
static uint8_t bandDelta8[MAX_BANDS] = {0};
static uint8_t globalVis8 = 0;
static uint8_t bassVis8 = 0;
static uint8_t midVis8 = 0;
static uint8_t trebleVis8 = 0;
static uint16_t majorPeakHz = 0;
static uint8_t majorPeakMag = 0;
static uint8_t spectralFlux8 = 0;
static uint8_t peakDetected = 0;
static float globalVisAvg = 0.0f;

// Channel selection by USB serial number (moved up for chord detection)
static const char* SERIAL_RIGHT = "16102920";
static const char* SERIAL_LEFT = "19236400";
static AudioAnalyzeFFT1024* activeFft = &fftLeft;
static const char* activeChannelLabel = "LEFT (ch0)";

// ============================================================================
// PITCH DETECTION - Chroma extraction for dominant pitch and harmonic fingerprint
// ============================================================================
// Pitch classes: 0=C, 1=C#, 2=D, 3=D#, 4=E, 5=F, 6=F#, 7=G, 8=G#, 9=A, 10=A#, 11=B

const int NUM_PITCH_CLASSES = 12;
const char* pitchNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

// Chroma state
float chromaRaw[NUM_PITCH_CLASSES] = {0};      // Raw chroma from current FFT
float chromaSmoothed[NUM_PITCH_CLASSES] = {0}; // Smoothed chroma for stability
const float chromaSmoothFactor = 0.3f;         // Higher = more smoothing

// Pitch detection output
uint8_t chromaOut[NUM_PITCH_CLASSES];  // Quantized chroma (0-255) for transmission
uint8_t dominantPitch = 255;           // 0-11 = pitch class, 255 = none
uint8_t dominantPitchStrength = 0;     // Strength of dominant pitch (0-255)

// ============================================================================
// VOCAL ENVELOPE + SYLLABLE DETECTION (vocal-focused energy + transient gating)
// ============================================================================
const int VOCAL_BIN_START = 2;   // ~86 Hz
const int VOCAL_BIN_END = 90;    // ~3.9 kHz
const float VOCAL_ENV_GAIN = 4000.0f;
const float VOCAL_ENV_SCALE = 1.8f;     // Scales gated energy into 0..1
const float VOCAL_NOISE_ALPHA = 0.995f; // Slow noise floor tracking
const float VOCAL_NOISE_MULT = 1.05f;   // Gate above noise floor (more permissive)
const float VOCAL_ATTACK = 0.45f;
const float VOCAL_RELEASE = 0.12f;
const float VOCAL_HIT_THRESH = 0.12f;   // Envelope threshold for syllable (more permissive)
const float VOCAL_HIT_SLOPE = 0.02f;    // Rising edge threshold (more permissive)
const uint32_t VOCAL_MIN_GAP_MS = 80;
const uint8_t VOCAL_NOTE_MIN_STRENGTH = 40;
const uint8_t VOCAL_NOTE_CAPTURE_FRAMES = 10; // Frames after hit to allow late note capture
const float VOCAL_SUSTAIN_THRESH = 0.10f;  // Sustain engage threshold (env 0..1)
const float VOCAL_SUSTAIN_RELEASE = 0.06f; // Sustain release threshold (hysteresis)
const uint8_t VOCAL_SUSTAIN_STABLE_FRAMES = 4; // Stable pitch frames to confirm sustain

static float vocalNoiseFloor = 0.0f;
static float vocalEnvSmoothed = 0.0f;
static float vocalEnvPrev = 0.0f;
static uint8_t vocalEnvOut = 0;
static uint8_t vocalSyllableHit = 0;
static uint8_t vocalNoteOut = 255;
static uint8_t vocalNoteStrengthOut = 0;
static uint32_t lastSyllableMs = 0;
static uint8_t lastSyllableNote = 255;
static uint8_t lastSyllableStrength = 0;
static uint8_t vocalNoteCaptureRemaining = 0;
static uint8_t vocalSustain = 0;
static uint8_t sustainCandidate = 255;
static uint8_t sustainStableCount = 0;

// Precomputed bin-to-pitch-class mapping
// FFT1024 @ 44.1kHz: bin_freq = bin * 43.07Hz
// We use bins 2-120 (~86Hz to ~5.2kHz) for harmonic content
const int CHROMA_BIN_START = 2;
const int CHROMA_BIN_END = 120;
int8_t binToPitchClass[CHROMA_BIN_END + 1];  // -1 = not used

// Initialize bin-to-pitch-class lookup table
void initChromaMapping() {
    const float binFreqHz = 44100.0f / 1024.0f;  // ~43.07 Hz per bin
    const float C0 = 16.3516f;  // C0 frequency

    for (int bin = 0; bin <= CHROMA_BIN_END; bin++) {
        if (bin < CHROMA_BIN_START) {
            binToPitchClass[bin] = -1;
            continue;
        }
        float freq = bin * binFreqHz;
        // Calculate pitch class: 12 * log2(freq / C0) mod 12
        float semitones = 12.0f * log2f(freq / C0);
        int pitchClass = ((int)roundf(semitones)) % 12;
        if (pitchClass < 0) pitchClass += 12;
        binToPitchClass[bin] = pitchClass;
    }
}

// Extract chroma from FFT bins and find dominant pitch
void calculateChroma() {
    // Clear raw chroma
    for (int i = 0; i < NUM_PITCH_CLASSES; i++) {
        chromaRaw[i] = 0.0f;
    }

    // Accumulate energy into pitch classes
    for (int bin = CHROMA_BIN_START; bin <= CHROMA_BIN_END; bin++) {
        int pc = binToPitchClass[bin];
        if (pc >= 0 && pc < NUM_PITCH_CLASSES) {
            float mag = activeFft->read(bin);
            chromaRaw[pc] += mag * mag;  // Use power for better discrimination
        }
    }

    // Apply sqrt for perceptual scaling and smooth
    float maxChroma = 0.0f;
    float totalChroma = 0.0f;
    for (int i = 0; i < NUM_PITCH_CLASSES; i++) {
        chromaRaw[i] = sqrtf(chromaRaw[i]);
        chromaSmoothed[i] = chromaSmoothFactor * chromaSmoothed[i] +
                           (1.0f - chromaSmoothFactor) * chromaRaw[i];
        totalChroma += chromaSmoothed[i];
        if (chromaSmoothed[i] > maxChroma) maxChroma = chromaSmoothed[i];
    }

    // Normalize and quantize to 0-255
    for (int i = 0; i < NUM_PITCH_CLASSES; i++) {
        float normalized = (maxChroma > 0.001f) ? (chromaSmoothed[i] / maxChroma) : 0.0f;
        chromaOut[i] = (uint8_t)(normalized * 255.0f);
    }

    // Find dominant pitch class with confidence
    int maxIdx = 0;
    for (int i = 1; i < NUM_PITCH_CLASSES; i++) {
        if (chromaOut[i] > chromaOut[maxIdx]) maxIdx = i;
    }

    // Calculate how dominant the peak is vs average (confidence measure)
    float avgChroma = totalChroma / 12.0f;
    float peakRatio = (avgChroma > 0.001f) ? (chromaSmoothed[maxIdx] / avgChroma) : 0.0f;

    // Dominant pitch needs to be significantly above average (ratio > 1.5)
    if (chromaOut[maxIdx] > 30 && peakRatio > 1.5f) {
        dominantPitch = maxIdx;
        dominantPitchStrength = min(255, (int)((peakRatio - 1.0f) * 100.0f));
    } else {
        dominantPitch = 255;
        dominantPitchStrength = 0;
    }
}

// Calculate vocal envelope + syllable trigger + note (pitch class)
void calculateVocalEnvelope() {
    float sumEnergy = 0.0f;
    const int binCount = VOCAL_BIN_END - VOCAL_BIN_START + 1;
    for (int bin = VOCAL_BIN_START; bin <= VOCAL_BIN_END; bin++) {
        float a = activeFft->read(bin);
        sumEnergy += a * a;
    }

    float rms = sqrtf(sumEnergy / binCount);
    float raw = rms * VOCAL_ENV_GAIN;

    if (vocalNoiseFloor <= 0.0001f) {
        vocalNoiseFloor = raw;
    } else {
        vocalNoiseFloor = (VOCAL_NOISE_ALPHA * vocalNoiseFloor) + ((1.0f - VOCAL_NOISE_ALPHA) * raw);
    }

    float gated = raw - (vocalNoiseFloor * VOCAL_NOISE_MULT);
    if (gated < 0.0f) gated = 0.0f;

    float env = gated * VOCAL_ENV_SCALE;
    if (env > 1.0f) env = 1.0f;

    float coeff = (env > vocalEnvSmoothed) ? VOCAL_ATTACK : VOCAL_RELEASE;
    vocalEnvSmoothed += (env - vocalEnvSmoothed) * coeff;
    if (vocalEnvSmoothed < 0.0f) vocalEnvSmoothed = 0.0f;

    vocalEnvOut = static_cast<uint8_t>(vocalEnvSmoothed * 255.0f + 0.5f);

    float delta = vocalEnvSmoothed - vocalEnvPrev;
    uint32_t now = millis();
    bool hit = (vocalEnvSmoothed > VOCAL_HIT_THRESH) &&
               (delta > VOCAL_HIT_SLOPE) &&
               (now - lastSyllableMs > VOCAL_MIN_GAP_MS);

    uint8_t curPitch = 255;
    uint8_t curStrength = 0;
    if (dominantPitch < 12 && dominantPitchStrength >= VOCAL_NOTE_MIN_STRENGTH) {
        curPitch = dominantPitch;
        curStrength = dominantPitchStrength;
    }

    vocalSyllableHit = hit ? 1 : 0;
    if (hit) {
        lastSyllableMs = now;
        vocalNoteCaptureRemaining = VOCAL_NOTE_CAPTURE_FRAMES;
        if (curPitch < 12) {
            vocalNoteOut = curPitch;
            vocalNoteStrengthOut = curStrength;
            vocalNoteCaptureRemaining = 0;
        } else {
            if (!vocalSustain) {
                vocalNoteOut = 255;
                vocalNoteStrengthOut = 0;
            }
        }
        lastSyllableNote = vocalNoteOut;
        lastSyllableStrength = vocalNoteStrengthOut;

        if (Serial) {
            Serial.print("[Vocal] syllable env=");
            Serial.print(vocalEnvOut);
            Serial.print(" note=");
            if (vocalNoteOut < 12) {
                Serial.print(pitchNames[vocalNoteOut]);
                Serial.print("(");
                Serial.print(vocalNoteStrengthOut);
                Serial.print(")");
            } else {
                Serial.print("--");
            }
            Serial.println();
        }
    } else if (vocalNoteCaptureRemaining > 0) {
        if (curPitch < 12) {
            vocalNoteOut = curPitch;
            vocalNoteStrengthOut = curStrength;
            lastSyllableNote = vocalNoteOut;
            lastSyllableStrength = vocalNoteStrengthOut;
            vocalNoteCaptureRemaining = 0;
            if (Serial) {
                Serial.print("[Vocal] late note=");
                Serial.print(pitchNames[vocalNoteOut]);
                Serial.print("(");
                Serial.print(vocalNoteStrengthOut);
                Serial.println(")");
            }
        } else {
            vocalNoteCaptureRemaining--;
        }
    }

    // Sustain detection: hold note while env stays high and pitch is stable
    if (vocalEnvSmoothed >= VOCAL_SUSTAIN_THRESH && curPitch < 12) {
        if (curPitch == sustainCandidate) {
            if (sustainStableCount < 255) sustainStableCount++;
        } else {
            sustainCandidate = curPitch;
            sustainStableCount = 1;
        }
        if (sustainStableCount >= VOCAL_SUSTAIN_STABLE_FRAMES) {
            vocalSustain = 1;
            vocalNoteOut = curPitch;
            vocalNoteStrengthOut = curStrength;
        }
    } else if (vocalEnvSmoothed <= VOCAL_SUSTAIN_RELEASE) {
        vocalSustain = 0;
        sustainCandidate = 255;
        sustainStableCount = 0;
        vocalNoteOut = 255;
        vocalNoteStrengthOut = 0;
    }

    vocalEnvPrev = vocalEnvSmoothed;
}

// ============================================================================
// Peak frequency + visual bands + transient features (AUX payload)
// ============================================================================
static void calculatePeakData() {
    int peakBin = PEAK_BIN_START;
    float peakMag = 0.0f;

    for (int bin = PEAK_BIN_START; bin <= PEAK_BIN_END; bin++) {
        float mag = activeFft->read(bin);
        if (mag > peakMag) {
            peakMag = mag;
            peakBin = bin;
        }
    }

    float refinedBin = (float)peakBin;
    if (peakBin > PEAK_BIN_START && peakBin < PEAK_BIN_END) {
        float alpha = activeFft->read(peakBin - 1);
        float beta = activeFft->read(peakBin);
        float gamma = activeFft->read(peakBin + 1);
        float denom = alpha - 2.0f * beta + gamma;
        float delta = (fabsf(denom) > 1.0e-12f) ? (0.5f * (alpha - gamma) / denom) : 0.0f;
        refinedBin += delta;
    }

    float hz = refinedBin * BIN_FREQ_HZ;
    if (hz < 0.0f) hz = 0.0f;
    if (hz > 22050.0f) hz = 22050.0f;
    majorPeakHz = (uint16_t)(hz + 0.5f);

    float peakNorm = log1pf(PEAK_MAG_LOG_K * peakMag) / log1pf(PEAK_MAG_LOG_K);
    if (peakNorm < 0.0f) peakNorm = 0.0f;
    if (peakNorm > 1.0f) peakNorm = 1.0f;
    majorPeakMag = (uint8_t)(peakNorm * 255.0f + 0.5f);
}

static void calculateVisualBands() {
    float sumSq = 0.0f;
    float fluxSum = 0.0f;

    for (int i = 0; i < BANDS_12; i++) {
        float raw = smoothedBandAmplitude[i];
        raw *= bandGainVis[i];

        float scaled = raw * VIS_SCALE;
        float compressed = log1pf(VIS_LOG_K * scaled) / log1pf(VIS_LOG_K);
        float target = constrain(compressed, 0.0f, 1.0f);

        float prev = bandVisFft[i];
        bandVisFft[i] = prev * 0.7f + target * 0.3f;

        float delta = bandVisFft[i] - prev;
        if (delta < 0.0f) delta = 0.0f;

        bandVis8[i] = (uint8_t)constrain(bandVisFft[i] * 255.0f + 0.5f, 0.0f, 255.0f);
        bandDelta8[i] = (uint8_t)constrain(delta * VIS_DELTA_GAIN * 255.0f + 0.5f, 0.0f, 255.0f);

        sumSq += bandVisFft[i] * bandVisFft[i];
        fluxSum += delta;
    }

    float g = sqrtf(sumSq / BANDS_12);
    g = constrain(g, 0.0f, 1.0f);
    globalVis8 = (uint8_t)(g * 255.0f + 0.5f);

    float bass = (bandVisFft[0] + bandVisFft[1] + bandVisFft[2]) / 3.0f;
    float mid = (bandVisFft[3] + bandVisFft[4] + bandVisFft[5] + bandVisFft[6] + bandVisFft[7]) / 5.0f;
    float treble = (bandVisFft[8] + bandVisFft[9] + bandVisFft[10] + bandVisFft[11]) / 4.0f;

    bassVis8 = (uint8_t)constrain(bass * 255.0f + 0.5f, 0.0f, 255.0f);
    midVis8 = (uint8_t)constrain(mid * 255.0f + 0.5f, 0.0f, 255.0f);
    trebleVis8 = (uint8_t)constrain(treble * 255.0f + 0.5f, 0.0f, 255.0f);

    float flux = fluxSum * VIS_FLUX_GAIN;
    if (flux < 0.0f) flux = 0.0f;
    if (flux > 1.0f) flux = 1.0f;
    spectralFlux8 = (uint8_t)(flux * 255.0f + 0.5f);

    if (globalVisAvg <= 0.0001f) {
        globalVisAvg = g;
    } else {
        globalVisAvg = (globalVisAvg * (1.0f - PEAK_AVG_ALPHA)) + (g * PEAK_AVG_ALPHA);
    }

    float avg = (globalVisAvg > 0.0001f) ? globalVisAvg : g;
    peakDetected = (g > (avg * PEAK_DETECT_RATIO) && g > PEAK_DETECT_MIN) ? 1 : 0;
}

// Communication protocol (fixed 68-byte payload for all frames)
// Layout: 12 floats (48) + vocal bytes (4) + spdif (1) + chroma[12] + dominantPitch + pitchStrength + sustain
namespace Proto {
static const uint8_t SOF = 0xAA;
static const uint8_t EOF_BYTE = 0xBB;
static const uint8_t TYPE_FFT = 0x01;
static const uint8_t TYPE_CMD = 0x02;
static const uint8_t TYPE_AUX = 0x03;
static const uint8_t PAYLOAD_LEN = 68;  // 48 + 4 + 1 + 12 + 1 + 1 + 1 = 68
static const uint8_t AUX_PAYLOAD_LEN = 36;
static const uint8_t CMD_PAYLOAD_LEN = 68;  // Commands are fixed-size frames (match FFT payload length)

inline uint16_t crc16_ccitt(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= static_cast<uint16_t>(data[i]) << 8;
        for (int b = 0; b < 8; ++b) {
            crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : (crc << 1);
        }
    }
    return crc;
}

inline bool encodeFrame(uint8_t type, uint8_t seq,
                        const uint8_t* payload, uint8_t payloadLen,
                        uint8_t* out, size_t outCap, size_t& outLen) {
    size_t needed = 1 + 1 + 1 + 1 + payloadLen + 2 + 1;
    if (outCap < needed) return false;

    out[0] = SOF;
    out[1] = type;
    out[2] = seq;
    out[3] = payloadLen;
    memcpy(&out[4], payload, payloadLen);

    uint16_t crc = crc16_ccitt(&out[1], 3 + payloadLen);
    out[4 + payloadLen] = crc & 0xFF;
    out[5 + payloadLen] = crc >> 8;
    out[6 + payloadLen] = EOF_BYTE;

    outLen = needed;
    return true;
}
}  // namespace Proto

const uint32_t ESP32_BAUD_RATE = 38400;
const uint32_t LED_BAUD_RATE = 460800;

// Frame timing
unsigned long lastFrameTime = 0;
const unsigned long FRAME_PERIOD_MS = 17;  // ~60 FPS

// Status timing
unsigned long lastStatusTime = 0;
const unsigned long STATUS_INTERVAL_MS = 5000;

unsigned long fftFrameCountWindow = 0;
unsigned long lastFftFrameReport = 0;
const unsigned long FFT_REPORT_INTERVAL_MS = 2000;
static bool fftFrameSeenSinceStatus = false;

// CPU usage tracking
static uint32_t cpuIntervalStartMicros = 0;
static uint64_t cpuBusySumMicros = 0;
static uint32_t cpuLoopMaxMicros = 0;
static uint32_t cpuLoopCount = 0;

// Beat detection removed

// ============================================================================
// Calculate smoothed band amplitudes from FFT + chord detection
// ============================================================================
void calculateBandAmplitudes() {
    if (activeFft->available()) {
        fftFrameCountWindow++;
        fftFrameSeenSinceStatus = true;

        // Calculate band amplitudes
        for (int band = 0; band < activeBands; band++) {
            float sumEnergy = 0.0f;
            int binStart, binEnd;
            float tilt;

            if (activeBands == 12) {
                binStart = binGroups12[band][0];
                binEnd = binGroups12[band][1];
                tilt = bandTilt12[band];
            } else {
                binStart = binGroups10[band][0];
                binEnd = binGroups10[band][1];
                tilt = bandTilt10[band];
            }

            int binCount = binEnd - binStart + 1;

            for (int bin = binStart; bin <= binEnd; bin++) {
                float a = activeFft->read(bin);
                sumEnergy += a * a;
            }

            float bandPower = sumEnergy / binCount;
            float bandEnergy = sqrtf(bandPower);
            float calibratedEnergy = bandEnergy * FFT_CAL_GAIN;
            calibratedEnergy *= tilt;
            lastCalibratedEnergy[band] = calibratedEnergy;

            smoothedBandAmplitude[band] = (smoothingFactor * smoothedBandAmplitude[band]) +
                                          ((1 - smoothingFactor) * calibratedEnergy);
        }

        // Calculate chroma and dominant pitch
        calculateChroma();

        // Calculate vocal envelope and syllable trigger
        calculateVocalEnvelope();

        // Peak frequency scan and visual band features
        calculatePeakData();
        calculateVisualBands();
    }
}

// ============================================================================
// Transmit FFT + vocal data to LED controller
// ============================================================================
static bool wasTransmitting = false;
static uint8_t fftSeq = 0;
static uint8_t cmdSeq = 0;
static uint8_t auxSeq = 0;

static void sendAuxFrame();

void sendFftFrame() {
    bool hasSignal = false;
    for (int i = 0; i < activeBands; i++) {
        if (smoothedBandAmplitude[i] > 0.00000001f) {
            hasSignal = true;
            break;
        }
    }

    if (!hasSignal) {
        if (wasTransmitting) {
            wasTransmitting = false;
        }
        return;
    }

    if (!wasTransmitting) {
        wasTransmitting = true;
    }

    // Build payload (68 bytes):
    // [0-47]  12 floats: band amplitudes
    // [48-51] vocal envelope/flags:
    //         [48]=vocalEnv (0-255), [49]=syllableHit (0/1)
    //         [50]=vocalNote (0-11, 255=none), [51]=vocalNoteStrength (0-255)
    // [52]    spdif lock
    // [53-64] chroma[12]: pitch class energies (0-255 each)
    // [65]    dominantPitch (0-11 = pitch class, 255 = none)
    // [66]    dominantPitchStrength (0-255)
    // [67]    vocalSustain (0/1)
    uint8_t payload[Proto::PAYLOAD_LEN] = {0};
    float bands12[BANDS_12] = {0.0f};
    for (int i = 0; i < activeBands && i < BANDS_12; i++) {
        bands12[i] = smoothedBandAmplitude[i];
    }

    memcpy(&payload[0], bands12, BANDS_12 * sizeof(float));
    payload[48] = vocalEnvOut;
    payload[49] = vocalSyllableHit;
    payload[50] = vocalNoteOut;
    payload[51] = vocalNoteStrengthOut;
    payload[52] = spdifInput.pllLocked() ? 1 : 0;

    // Add pitch detection data
    memcpy(&payload[53], chromaOut, NUM_PITCH_CLASSES);
    payload[65] = dominantPitch;
    payload[66] = dominantPitchStrength;
    payload[67] = vocalSustain;

    uint8_t frame[1 + 1 + 1 + 1 + Proto::PAYLOAD_LEN + 2 + 1];
    size_t frameLen = 0;
    if (Proto::encodeFrame(Proto::TYPE_FFT, fftSeq++, payload, Proto::PAYLOAD_LEN, frame, sizeof(frame), frameLen)) {
        Serial2.write(frame, frameLen);
    }

    sendAuxFrame();
}

// AUX payload (36 bytes):
// [0-11]  bandVis[12]   (0-255)
// [12-23] bandDelta[12] (0-255)
// [24]    globalVis
// [25]    bassVis
// [26]    midVis
// [27]    trebleVis
// [28-29] majorPeakHz (uint16, Hz)
// [30]    majorPeakMag (0-255)
// [31]    spectralFlux (0-255)
// [32]    peakDetected (0/1)
// [33]    activeBands
// [34-35] reserved
static void sendAuxFrame() {
    uint8_t payload[Proto::AUX_PAYLOAD_LEN] = {0};
    memcpy(&payload[0], bandVis8, BANDS_12);
    memcpy(&payload[12], bandDelta8, BANDS_12);
    payload[24] = globalVis8;
    payload[25] = bassVis8;
    payload[26] = midVis8;
    payload[27] = trebleVis8;
    payload[28] = static_cast<uint8_t>(majorPeakHz & 0xFF);
    payload[29] = static_cast<uint8_t>((majorPeakHz >> 8) & 0xFF);
    payload[30] = majorPeakMag;
    payload[31] = spectralFlux8;
    payload[32] = peakDetected;
    payload[33] = static_cast<uint8_t>(activeBands);

    uint8_t frame[1 + 1 + 1 + 1 + Proto::AUX_PAYLOAD_LEN + 2 + 1];
    size_t frameLen = 0;
    if (Proto::encodeFrame(Proto::TYPE_AUX, auxSeq++, payload, Proto::AUX_PAYLOAD_LEN, frame, sizeof(frame), frameLen)) {
        Serial2.write(frame, frameLen);
    }
}

// ============================================================================
// Forward ESP32 commands to LED Teensy
// ============================================================================
void forwardESP32Commands() {
    if (!Serial1.available()) return;

    char command[64];
    size_t len = Serial1.readBytesUntil('\n', command, sizeof(command) - 1);
    command[len] = '\0';

    // Trim trailing CR
    if (len > 0 && command[len - 1] == '\r') {
        command[len - 1] = '\0';
        len--;
    }

    Serial.printf("[CMD] Received: '%s'\n", command);

    // Parse and forward mode commands
    uint8_t payload[Proto::CMD_PAYLOAD_LEN] = {0};
    char* p = command;
    if (*p == 'C') p++;

    char mode = *p ? *p : '0';
    p++;
    if (*p == ',') p++;

    int pattern = 0;
    int brightness = 0;

    if (strchr(p, ',')) {
        char* saveptr = nullptr;
        char* token = strtok_r(p, ",", &saveptr);
        if (token) pattern = atoi(token);
        token = strtok_r(nullptr, ",", &saveptr);
        if (token) brightness = atoi(token);
    } else {
        while (*p == ' ' || *p == '\r' || *p == '\t') p++;
        if (isdigit(static_cast<unsigned char>(*p))) {
            pattern = *p - '0';
            p++;
        }
        if (isdigit(static_cast<unsigned char>(*p))) {
            p++;
        }
        if (isdigit(static_cast<unsigned char>(*p))) {
            brightness = atoi(p);
        }
    }

    payload[0] = static_cast<uint8_t>(mode);
    payload[1] = static_cast<uint8_t>(pattern);
    payload[2] = static_cast<uint8_t>(pattern);
    payload[3] = static_cast<uint8_t>(brightness);

    // Always use 12 bands for all music patterns to keep visuals consistent
    if (mode == 'M') {
        currentPattern = pattern;
        int newBands = BANDS_12;
        if (newBands != activeBands) {
            activeBands = newBands;
            // Clear smoothed values when switching band counts
            for (int i = 0; i < MAX_BANDS; i++) {
                smoothedBandAmplitude[i] = 0;
                lastCalibratedEnergy[i] = 0;
                bandVisFft[i] = 0.0f;
                bandVis8[i] = 0;
                bandDelta8[i] = 0;
            }
            globalVisAvg = 0.0f;
            Serial.printf("[FFT] Switched to %d bands for pattern %d\n", activeBands, pattern);
        }
    }

    uint8_t frame[1 + 1 + 1 + 1 + Proto::CMD_PAYLOAD_LEN + 2 + 1];
    size_t frameLen = 0;
    if (Proto::encodeFrame(Proto::TYPE_CMD, cmdSeq++, payload, Proto::CMD_PAYLOAD_LEN, frame, sizeof(frame), frameLen)) {
        Serial2.write(frame, frameLen);
    }

    if (Serial) {
        Serial.printf("Mode: %c, Pattern: %d, Brightness: %d, Bands: %d\n", mode, pattern, brightness, activeBands);
    }
}

// ============================================================================
// Setup
// ============================================================================
void setup() {
    Serial.begin(115200);
    delay(3000);

    if (strcmp(usb_string_serial_number, SERIAL_RIGHT) == 0) {
        activeFft = &fftRight;
        activeChannelLabel = "RIGHT (ch1)";
    } else if (strcmp(usb_string_serial_number, SERIAL_LEFT) == 0) {
        activeFft = &fftLeft;
        activeChannelLabel = "LEFT (ch0)";
    } else {
        activeFft = &fftLeft;
        activeChannelLabel = "LEFT (ch0) [DEFAULT]";
    }

    Serial.println("\n=== Teensy FFT Bridge ===");
    Serial.print("Serial#: ");
    Serial.println(usb_string_serial_number);
    Serial.print("SPDIF Channel: ");
    Serial.println(activeChannelLabel);
    Serial.print("Firmware: teensy_fft | Built: ");
    Serial.print(__DATE__); Serial.print(" "); Serial.println(__TIME__);
    Serial.println("Beat detection: DISABLED (vocal envelope only)");
    Serial.println("Pitch detection: LOCAL (chroma + dominant pitch)");
    Serial.println("========================================\n");

    // Initialize chord detection lookup table
    initChromaMapping();

    AudioMemory(60);
    delay(500);

    fftLeft.windowFunction(AudioWindowHanning1024);
    fftRight.windowFunction(AudioWindowHanning1024);
    delay(100);

    Serial1.begin(ESP32_BAUD_RATE);
    delay(100);

    Serial2.begin(LED_BAUD_RATE);
    delay(100);

    cpuIntervalStartMicros = micros();
}

// ============================================================================
// Main Loop
// ============================================================================
void loop() {
    unsigned long now = millis();
    uint32_t loopStartMicros = micros();

    // Forward ESP32 commands to LED Teensy
    forwardESP32Commands();

    // Calculate FFT amplitudes
    calculateBandAmplitudes();

    // Transmit at ~60 FPS
    if (now - lastFrameTime >= FRAME_PERIOD_MS) {
        lastFrameTime = now;
        sendFftFrame();
    }

    // Status output every 5 seconds
    if (Serial && now - lastStatusTime >= STATUS_INTERVAL_MS) {
        lastStatusTime = now;
        uint32_t intervalMicros = micros() - cpuIntervalStartMicros;
        float avgCpuPct = 0.0f;
        float peakCpuPct = 0.0f;
        if (intervalMicros > 0 && cpuLoopCount > 0) {
            float avgLoopPeriod = static_cast<float>(intervalMicros) / static_cast<float>(cpuLoopCount);
            avgCpuPct = (static_cast<float>(cpuBusySumMicros) / static_cast<float>(intervalMicros)) * 100.0f;
            peakCpuPct = (static_cast<float>(cpuLoopMaxMicros) / avgLoopPeriod) * 100.0f;
        }
        Serial.print("FFT SPDIF:");
        Serial.print(spdifInput.pllLocked() ? "LOCK" : "NOLOCK");
        // Dominant pitch info
        Serial.print(" Note:");
        if (dominantPitch < 12) {
            Serial.print(pitchNames[dominantPitch]);
            Serial.print("(");
            Serial.print(dominantPitchStrength);
            Serial.print(")");
        } else {
            Serial.print("--");
        }
        Serial.print(" VocalEnv:");
        Serial.print(vocalEnvOut);
        Serial.print(" Syll:");
        Serial.print(vocalSyllableHit);
        Serial.print(" SNote:");
        if (lastSyllableNote < 12) {
            Serial.print(pitchNames[lastSyllableNote]);
            Serial.print("(");
            Serial.print(lastSyllableStrength);
            Serial.print(")");
        } else {
            Serial.print("--");
        }
        Serial.print(" Hold:");
        if (vocalSustain && vocalNoteOut < 12) {
            Serial.print(pitchNames[vocalNoteOut]);
            Serial.print("(");
            Serial.print(vocalNoteStrengthOut);
            Serial.print(")");
        } else {
            Serial.print("--");
        }

        Serial.print(" CPU:");
        Serial.print(avgCpuPct, 1);
        Serial.print("%/");
        Serial.print(peakCpuPct, 1);
        Serial.print("%");
        Serial.print(" Bands(");
        Serial.print(activeBands);
        Serial.print("):");
        for (int i = 0; i < activeBands; i++) {
            Serial.print(smoothedBandAmplitude[i], 4);
            if (i < activeBands - 1) Serial.print(",");
        }
        Serial.println();

        cpuIntervalStartMicros = micros();
        cpuBusySumMicros = 0;
        cpuLoopMaxMicros = 0;
        cpuLoopCount = 0;
        fftFrameSeenSinceStatus = false;
    }

    // FFT frame report
    if (now - lastFftFrameReport >= FFT_REPORT_INTERVAL_MS) {
        fftFrameCountWindow = 0;
        lastFftFrameReport = now;
    }

    uint32_t loopMicros = micros() - loopStartMicros;
    cpuBusySumMicros += loopMicros;
    if (loopMicros > cpuLoopMaxMicros) cpuLoopMaxMicros = loopMicros;
    cpuLoopCount++;
}
