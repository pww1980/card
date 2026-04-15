#include "recorder.h"
#include "config.h"

#include <M5Cardputer.h>
#include <SD.h>

// ── WAV-Header ────────────────────────────────────────────────────────────────
struct WavHeader {
    char     riff[4]       = {'R','I','F','F'};
    uint32_t chunkSize     = 0;
    char     wave[4]       = {'W','A','V','E'};
    char     fmt[4]        = {'f','m','t',' '};
    uint32_t fmtSize       = 16;
    uint16_t audioFormat   = 1;
    uint16_t numChannels   = CHANNELS;
    uint32_t sampleRate    = SAMPLE_RATE;
    uint32_t byteRate      = SAMPLE_RATE * CHANNELS * (BIT_DEPTH / 8);
    uint16_t blockAlign    = CHANNELS * (BIT_DEPTH / 8);
    uint16_t bitsPerSample = BIT_DEPTH;
    char     dataTag[4]    = {'d','a','t','a'};
    uint32_t dataSize      = 0;
};

// ── Zustandsvariablen ─────────────────────────────────────────────────────────
static File      g_file;
static bool      g_running   = false;
static uint32_t  g_startMs   = 0;
static uint32_t  g_dataBytes = 0;

static int       g_gain  = 4;    // Software-Gain 1–16
static int       g_level = 0;    // Pegel 0–100 (letzter Buffer)

static int16_t   g_buf[REC_BUFFER_SIZE];

// ── Öffentliche Funktionen ────────────────────────────────────────────────────

bool Recorder::start(const String& filePath) {
    if (g_running) return false;

    // config() gibt einen rvalue zurück → per Copy holen, ändern, zurückschreiben
    auto mic_cfg = M5Cardputer.Mic.config();
    mic_cfg.sample_rate   = SAMPLE_RATE;
    mic_cfg.stereo        = false;
    mic_cfg.use_adc       = false;
    mic_cfg.over_sampling = 1;
    M5Cardputer.Mic.config(mic_cfg);

    if (!M5Cardputer.Mic.begin()) {
        Serial.println("[Recorder] Mic.begin() fehlgeschlagen");
        return false;
    }

    g_file = SD.open(filePath, FILE_WRITE);
    if (!g_file) {
        M5Cardputer.Mic.end();
        Serial.println("[Recorder] SD-Datei konnte nicht angelegt werden");
        return false;
    }

    WavHeader hdr;
    g_file.write(reinterpret_cast<const uint8_t*>(&hdr), sizeof(WavHeader));

    g_dataBytes = 0;
    g_level     = 0;
    g_startMs   = millis();
    g_running   = true;

    Serial.printf("[Recorder] Start: %s  Gain: %dx\n", filePath.c_str(), g_gain);
    return true;
}

void Recorder::stop() {
    if (!g_running) return;
    g_running = false;

    M5Cardputer.Mic.end();

    WavHeader hdr;
    hdr.dataSize  = g_dataBytes;
    hdr.chunkSize = g_dataBytes + sizeof(WavHeader) - 8;
    g_file.seek(0);
    g_file.write(reinterpret_cast<const uint8_t*>(&hdr), sizeof(WavHeader));
    g_file.close();

    Serial.printf("[Recorder] Stop: %lu s  %lu Bytes\n",
                  elapsedSeconds(), g_dataBytes);
}

void Recorder::tick() {
    if (!g_running) return;

    if (!M5Cardputer.Mic.record(g_buf, REC_BUFFER_SIZE, SAMPLE_RATE, false))
        return;

    // ── Software-Gain anwenden + Peak-Pegel berechnen ────────────────────────
    int16_t peak = 0;
    for (int i = 0; i < REC_BUFFER_SIZE; i++) {
        int32_t s = (int32_t)g_buf[i] * g_gain;
        // Clipping verhindern
        if      (s >  32767) s =  32767;
        else if (s < -32768) s = -32768;
        g_buf[i] = (int16_t)s;

        int16_t a = (g_buf[i] < 0) ? -g_buf[i] : g_buf[i];
        if (a > peak) peak = a;
    }

    // Peak auf 0–100 normalisieren (32767 = 100%)
    g_level = (int)((int32_t)peak * 100 / 32767);

    size_t bytes = REC_BUFFER_SIZE * sizeof(int16_t);
    g_file.write(reinterpret_cast<const uint8_t*>(g_buf), bytes);
    g_dataBytes += bytes;
}

uint32_t Recorder::elapsedSeconds() {
    if (!g_running) return 0;
    return (millis() - g_startMs) / 1000;
}

bool Recorder::isRunning() { return g_running; }

void Recorder::setGain(int gain) {
    g_gain = (gain < 1) ? 1 : (gain > 16) ? 16 : gain;
    Serial.printf("[Recorder] Gain: %dx\n", g_gain);
}

int Recorder::getGain()  { return g_gain;  }
int Recorder::getLevel() { return g_level; }
