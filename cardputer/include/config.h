#pragma once

// ── WiFi ─────────────────────────────────────────────────────────────────────
#define WIFI_SSID   "DEIN_NETZWERK"
#define WIFI_PASS   "DEIN_PASSWORT"

// ── Server ───────────────────────────────────────────────────────────────────
#define SERVER_HOST "192.168.1.100"
#define SERVER_PORT 8000
#define UPLOAD_PATH "/upload"
#define STATUS_PATH "/status"

// ── Audio ────────────────────────────────────────────────────────────────────
#define SAMPLE_RATE     16000   // Hz – optimal für Whisper
#define BIT_DEPTH       16      // Bit
#define CHANNELS        1       // Mono
#define REC_BUFFER_SIZE 1024    // Samples pro Chunk

// ── SD-Karte ─────────────────────────────────────────────────────────────────
#define REC_DIR    "/rec"
#define QUEUE_FILE "/rec/queue.txt"   // Offline-Upload-Queue

// ── Display ──────────────────────────────────────────────────────────────────
#define DISPLAY_WIDTH  240
#define DISPLAY_HEIGHT 135

// ── Timeouts ─────────────────────────────────────────────────────────────────
#define WIFI_TIMEOUT_MS    10000
#define UPLOAD_TIMEOUT_MS  30000
