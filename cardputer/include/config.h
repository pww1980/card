#pragma once

// ── WiFi ─────────────────────────────────────────────────────────────────────
#define WIFI_SSID   "DEIN_NETZWERK"
#define WIFI_PASS   "DEIN_PASSWORT"

// ── Server ───────────────────────────────────────────────────────────────────
#define SERVER_HOST "192.168.1.100"
#define SERVER_PORT 8000
#define UPLOAD_PATH "/upload"
#define STATUS_PATH "/status"

// ── Audio – ES8311 Codec (Cardputer ADV) ─────────────────────────────────────
// Pins verifiziert aus M5Unified Quellcode (M5Unified.cpp, Callback cardputer_adv)
#define SAMPLE_RATE     16000   // Hz – optimal für Whisper
#define BIT_DEPTH       16      // Bit
#define CHANNELS        1       // Mono
#define REC_BUFFER_SIZE 1024    // Samples pro Chunk (int16_t)

// I2S – gemeinsamer Bus für Mic (DIN) und Speaker (DOUT)
#define I2S_PORT        I2S_NUM_1
#define PIN_I2S_BCLK    41      // Bit Clock
#define PIN_I2S_WS      43      // Word Select / LRCLK
#define PIN_I2S_DOUT    42      // Data Out → Speaker (DAC)
#define PIN_I2S_DIN     46      // Data In  ← Mikrofon (ADC)

// ES8311 Codec – I2C-Steuerbus
// Achtung: gleicher Bus wie Tastatur-Controller TCA8418
#define PIN_I2C_SDA     9
#define PIN_I2C_SCL     8
#define I2C_PORT        I2C_NUM_1
#define ES8311_I2C_ADDR 0x18

// ── SD-Karte ─────────────────────────────────────────────────────────────────
#define REC_DIR    "/rec"
#define QUEUE_FILE "/rec/queue.txt"   // Offline-Upload-Queue

// ── Display ──────────────────────────────────────────────────────────────────
#define DISPLAY_WIDTH  240
#define DISPLAY_HEIGHT 135

// ── Timeouts ─────────────────────────────────────────────────────────────────
#define WIFI_TIMEOUT_MS    10000
#define UPLOAD_TIMEOUT_MS  30000
