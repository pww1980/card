#include "recorder.h"
#include "config.h"

#include <M5Cardputer.h>
#include <SD.h>
#include <driver/i2s.h>

// ── WAV-Header ────────────────────────────────────────────────────────────────
struct WavHeader {
    // RIFF
    char     riff[4]        = {'R','I','F','F'};
    uint32_t chunkSize      = 0;           // wird am Ende gesetzt
    char     wave[4]        = {'W','A','V','E'};
    // fmt
    char     fmt[4]         = {'f','m','t',' '};
    uint32_t fmtSize        = 16;
    uint16_t audioFormat    = 1;           // PCM
    uint16_t numChannels    = CHANNELS;
    uint32_t sampleRate     = SAMPLE_RATE;
    uint32_t byteRate       = SAMPLE_RATE * CHANNELS * (BIT_DEPTH / 8);
    uint16_t blockAlign     = CHANNELS * (BIT_DEPTH / 8);
    uint16_t bitsPerSample  = BIT_DEPTH;
    // data
    char     dataTag[4]     = {'d','a','t','a'};
    uint32_t dataSize       = 0;           // wird am Ende gesetzt
};

// ── Interne Zustandsvariablen ─────────────────────────────────────────────────
static File      g_file;
static bool      g_running     = false;
static uint32_t  g_startMs     = 0;
static uint32_t  g_dataBytes   = 0;

static int16_t   g_buf[REC_BUFFER_SIZE];

// ── I2S-Konfiguration für M5Cardputer ADV (PDM-Mikrofon) ─────────────────────
static void i2sInit() {
    i2s_config_t cfg = {
        .mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_PDM),
        .sample_rate          = SAMPLE_RATE,
        .bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format       = I2S_CHANNEL_FMT_ONLY_RIGHT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count        = 8,
        .dma_buf_len          = REC_BUFFER_SIZE,
        .use_apll             = false,
        .tx_desc_auto_clear   = false,
        .fixed_mclk           = 0
    };

    // TODO: Pins an tatsächliche ADV-Hardware anpassen
    i2s_pin_config_t pins = {
        .bck_io_num   = I2S_PIN_NO_CHANGE,
        .ws_io_num    = 43,   // PDM CLK – bitte Schaltplan prüfen
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num  = 44    // PDM DATA – bitte Schaltplan prüfen
    };

    i2s_driver_install(I2S_NUM_0, &cfg, 0, nullptr);
    i2s_set_pin(I2S_NUM_0, &pins);
    i2s_zero_dma_buffer(I2S_NUM_0);
}

// ── Öffentliche Funktionen ────────────────────────────────────────────────────
bool Recorder::start(const String& filePath) {
    if (g_running) return false;

    g_file = SD.open(filePath, FILE_WRITE);
    if (!g_file) return false;

    // Platzhalter-Header schreiben (wird am Ende aktualisiert)
    WavHeader hdr;
    g_file.write(reinterpret_cast<uint8_t*>(&hdr), sizeof(WavHeader));

    i2sInit();

    g_dataBytes = 0;
    g_startMs   = millis();
    g_running   = true;
    return true;
}

void Recorder::stop() {
    if (!g_running) return;
    g_running = false;

    i2s_driver_uninstall(I2S_NUM_0);

    // WAV-Header mit korrekten Größen aktualisieren
    WavHeader hdr;
    hdr.dataSize  = g_dataBytes;
    hdr.chunkSize = g_dataBytes + sizeof(WavHeader) - 8;

    g_file.seek(0);
    g_file.write(reinterpret_cast<uint8_t*>(&hdr), sizeof(WavHeader));
    g_file.close();
}

void Recorder::tick() {
    if (!g_running) return;

    size_t bytesRead = 0;
    i2s_read(I2S_NUM_0,
             g_buf,
             sizeof(g_buf),
             &bytesRead,
             portMAX_DELAY);

    if (bytesRead > 0) {
        g_file.write(reinterpret_cast<uint8_t*>(g_buf), bytesRead);
        g_dataBytes += bytesRead;
    }
}

uint32_t Recorder::elapsedSeconds() {
    if (!g_running) return 0;
    return (millis() - g_startMs) / 1000;
}

bool Recorder::isRunning() {
    return g_running;
}
