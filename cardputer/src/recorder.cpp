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
static bool      g_running    = false;
static uint32_t  g_startMs    = 0;
static uint32_t  g_dataBytes  = 0;
static uint32_t  g_lastFlushMs = 0;

static int       g_gain  = 4;    // Software-Gain 1–16
static int       g_level = 0;    // Pegel 0–100 (letzter Buffer)
static bool      g_writeError = false;

static int16_t   g_buf[REC_BUFFER_SIZE];

// ── Öffentliche Funktionen ────────────────────────────────────────────────────

bool Recorder::start(const String& filePath) {
    if (g_running) return false;

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

    g_dataBytes   = 0;
    g_level       = 0;
    g_writeError  = false;
    g_startMs     = millis();
    g_lastFlushMs = millis();
    g_running     = true;

    Serial.printf("[Recorder] Start: %s  Gain: %dx\n", filePath.c_str(), g_gain);
    return true;
}

void Recorder::stop() {
    if (!g_running) return;
    g_running = false;

    M5Cardputer.Mic.end();

    // Letzten SD-Puffer leeren
    g_file.flush();

    WavHeader hdr;
    hdr.dataSize  = g_dataBytes;
    hdr.chunkSize = g_dataBytes + sizeof(WavHeader) - 8;
    g_file.seek(0);
    g_file.write(reinterpret_cast<const uint8_t*>(&hdr), sizeof(WavHeader));
    g_file.close();

    Serial.printf("[Recorder] Stop: %lu s  %lu Bytes  Fehler: %s\n",
                  elapsedSeconds(), g_dataBytes,
                  g_writeError ? "ja" : "nein");
}

void Recorder::tick() {
    if (!g_running || g_writeError) return;

    // wait=true: blockiert (~256 ms bei 4096 Samples / 16 kHz) bis der
    // I2S-DMA-Buffer voll ist. Verhindert das silent-dropout-Problem
    // das bei wait=false nach kurzer Zeit auftrat.
    if (!M5Cardputer.Mic.record(g_buf, REC_BUFFER_SIZE, SAMPLE_RATE, true))
        return;

    // ── Software-Gain + Peak-Pegel ───────────────────────────────────────────
    int16_t peak = 0;
    for (int i = 0; i < REC_BUFFER_SIZE; i++) {
        int32_t s = (int32_t)g_buf[i] * g_gain;
        if      (s >  32767) s =  32767;
        else if (s < -32768) s = -32768;
        g_buf[i] = (int16_t)s;

        int16_t a = (g_buf[i] < 0) ? -g_buf[i] : g_buf[i];
        if (a > peak) peak = a;
    }
    g_level = (int)((int32_t)peak * 100 / 32767);

    // ── SD schreiben ──────────────────────────────────────────────────────────
    size_t bytes    = REC_BUFFER_SIZE * sizeof(int16_t);
    size_t written  = g_file.write(reinterpret_cast<const uint8_t*>(g_buf), bytes);

    if (written != bytes) {
        // SD-Fehler: Aufnahme intern stoppen, WAV bleibt lesbar bis hierher
        Serial.printf("[Recorder] FEHLER: write %u/%u Bytes – SD voll?\n",
                      written, bytes);
        g_writeError = true;
        return;
    }
    g_dataBytes += (uint32_t)written;

    // ── Periodischer Flush (alle 10 s) ────────────────────────────────────────
    // Sichert Daten gegen Datenverlust bei unerwarteter Trennung.
    if (millis() - g_lastFlushMs >= 10000) {
        g_file.flush();
        g_lastFlushMs = millis();
    }
}

bool     Recorder::isRunning()  { return g_running && !g_writeError; }
bool     Recorder::hasError()   { return g_writeError; }
uint32_t Recorder::elapsedSeconds() {
    if (!g_running) return 0;
    // Berechne aus tatsächlich geschriebenen Bytes statt millis(),
    // damit die Anzeige bei Schreibfehlern nicht weiterläuft.
    return g_dataBytes / (SAMPLE_RATE * CHANNELS * (BIT_DEPTH / 8));
}

void Recorder::setGain(int gain) {
    g_gain = (gain < 1) ? 1 : (gain > 16) ? 16 : gain;
    Serial.printf("[Recorder] Gain: %dx\n", g_gain);
}

int Recorder::getGain()  { return g_gain;  }
int Recorder::getLevel() { return g_level; }
