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
// 4096 Samples = 256 ms pro I2S-Read (statt 64 ms bei 1024).
// Größerer Buffer → weniger SD-Write-Overhead, stabiler bei langen Aufnahmen.
#define REC_BUFFER_SIZE 4096

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
// SPI-Bus-Pins für SD-Karte (M5Cardputer Standard + ADV)
#define SD_SCK_PIN   40
#define SD_MISO_PIN  39
#define SD_MOSI_PIN  14
#define SD_CS_PIN    12
#define REC_DIR    "/rec"
#define QUEUE_FILE "/rec/queue.txt"   // Offline-Upload-Queue

// ── Display ──────────────────────────────────────────────────────────────────
#define DISPLAY_WIDTH  240
#define DISPLAY_HEIGHT 135

// ── Timeouts ─────────────────────────────────────────────────────────────────
#define WIFI_TIMEOUT_MS    10000
#define UPLOAD_TIMEOUT_MS  300000   // 5 min – reicht für ~220 MB bei ~6 Mbit/s WLAN
