#include "recorder.h"
#include "config.h"

#include <M5Cardputer.h>
#include <SD.h>

// ── WAV-Header ────────────────────────────────────────────────────────────────
// PCM, Mono, 16-bit, 16 kHz
struct WavHeader {
    char     riff[4]       = {'R','I','F','F'};
    uint32_t chunkSize     = 0;          // gesamt - 8 Byte; wird am Ende gesetzt
    char     wave[4]       = {'W','A','V','E'};
    char     fmt[4]        = {'f','m','t',' '};
    uint32_t fmtSize       = 16;
    uint16_t audioFormat   = 1;          // PCM
    uint16_t numChannels   = CHANNELS;
    uint32_t sampleRate    = SAMPLE_RATE;
    uint32_t byteRate      = SAMPLE_RATE * CHANNELS * (BIT_DEPTH / 8);
    uint16_t blockAlign    = CHANNELS * (BIT_DEPTH / 8);
    uint16_t bitsPerSample = BIT_DEPTH;
    char     dataTag[4]    = {'d','a','t','a'};
    uint32_t dataSize      = 0;          // wird am Ende gesetzt
};

// ── Interne Zustandsvariablen ─────────────────────────────────────────────────
static File      g_file;
static bool      g_running   = false;
static uint32_t  g_startMs   = 0;
static uint32_t  g_dataBytes = 0;

// DMA-Puffer: REC_BUFFER_SIZE int16-Samples = 2 * REC_BUFFER_SIZE Bytes
static int16_t   g_buf[REC_BUFFER_SIZE];

// ── Öffentliche Funktionen ────────────────────────────────────────────────────

bool Recorder::start(const String& filePath) {
    if (g_running) return false;

    // M5Unified Mic konfigurieren – erkennt ES8311 automatisch per I2C-Probe
    // und wählt die korrekten Pins (BCK=41, WS=43, DIN=46, I2S_NUM_1)
    auto& mic_cfg = M5Cardputer.Mic.config();
    mic_cfg.sample_rate  = SAMPLE_RATE;
    mic_cfg.stereo       = false;
    mic_cfg.use_adc      = false;        // digital I2S, kein ADC
    mic_cfg.over_sampling = 1;

    if (!M5Cardputer.Mic.begin()) {
        Serial.println("[Recorder] Mic.begin() fehlgeschlagen");
        return false;
    }

    // WAV-Datei anlegen
    g_file = SD.open(filePath, FILE_WRITE);
    if (!g_file) {
        M5Cardputer.Mic.end();
        Serial.println("[Recorder] SD-Datei konnte nicht angelegt werden");
        return false;
    }

    // Platzhalter-Header schreiben (Größen unbekannt bis Aufnahme endet)
    WavHeader hdr;
    g_file.write(reinterpret_cast<const uint8_t*>(&hdr), sizeof(WavHeader));

    g_dataBytes = 0;
    g_startMs   = millis();
    g_running   = true;

    Serial.printf("[Recorder] Start: %s\n", filePath.c_str());
    return true;
}

void Recorder::stop() {
    if (!g_running) return;
    g_running = false;

    M5Cardputer.Mic.end();

    // WAV-Header mit korrekten Größen aktualisieren
    WavHeader hdr;
    hdr.dataSize  = g_dataBytes;
    hdr.chunkSize = g_dataBytes + sizeof(WavHeader) - 8;

    g_file.seek(0);
    g_file.write(reinterpret_cast<const uint8_t*>(&hdr), sizeof(WavHeader));
    g_file.close();

    Serial.printf("[Recorder] Stop: %.1f s, %lu Bytes\n",
                  elapsedSeconds() * 1.0f, g_dataBytes);
}

void Recorder::tick() {
    if (!g_running) return;

    // record() füllt g_buf mit REC_BUFFER_SIZE Samples (int16, Mono)
    // blocking=false: gibt sofort zurück, false wenn kein Puffer verfügbar
    if (M5Cardputer.Mic.record(g_buf, REC_BUFFER_SIZE, SAMPLE_RATE, false)) {
        size_t bytes = REC_BUFFER_SIZE * sizeof(int16_t);
        g_file.write(reinterpret_cast<const uint8_t*>(g_buf), bytes);
        g_dataBytes += bytes;
    }
}

uint32_t Recorder::elapsedSeconds() {
    if (!g_running) return 0;
    return (millis() - g_startMs) / 1000;
}

bool Recorder::isRunning() {
    return g_running;
}
