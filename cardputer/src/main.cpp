#include <M5Cardputer.h>
#include <SD.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>

#include "config.h"
#include "recorder.h"
#include "uploader.h"
#include "display.h"

// ── Zustandsmaschine ─────────────────────────────────────────────────────────
enum class AppState {
    IDLE,
    RECORDING,
    UPLOADING,
    UPLOAD_OK,
    UPLOAD_FAIL,
    WIFI_CONNECT
};

static AppState  g_state     = AppState::IDLE;
static String    g_lastFile  = "";
static String    g_lastJobId = "";

// ── NTP-Zeit ──────────────────────────────────────────────────────────────────
static void syncTime() {
    configTime(3600, 3600, "pool.ntp.org", "time.google.com");
}

static String timestampFilename() {
    struct tm ti;
    if (!getLocalTime(&ti)) return "/rec/rec_unknown.wav";
    char buf[32];
    strftime(buf, sizeof(buf), "/rec/%Y%m%d_%H%M%S.wav", &ti);
    return String(buf);
}

// ── Setup ─────────────────────────────────────────────────────────────────────
void setup() {
    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);

    Serial.begin(115200);

    // Display initialisieren
    Display::init();
    Display::showMessage("Booting...");

    // SD-Karte
    if (!SD.begin(SD_CS_PIN)) {
        Display::showError("SD-Karte fehlt!");
        while (true) delay(1000);
    }
    if (!SD.exists(REC_DIR)) SD.mkdir(REC_DIR);

    // WiFi
    Display::showMessage("WiFi...");
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    unsigned long t = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t < WIFI_TIMEOUT_MS) {
        delay(200);
    }

    if (WiFi.status() == WL_CONNECTED) {
        syncTime();
        Display::showMessage("IP: " + WiFi.localIP().toString());
    } else {
        Display::showMessage("Offline-Modus");
    }

    delay(1000);
    Display::showIdle();
}

// ── Loop ──────────────────────────────────────────────────────────────────────
void loop() {
    M5Cardputer.update();

    // ── Tastatureingabe ───────────────────────────────────────────────────────
    if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
        Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();

        for (auto c : status.word) {
            switch (c) {
                // R: Aufnahme starten / stoppen
                case 'r':
                case 'R':
                    if (g_state == AppState::IDLE) {
                        g_lastFile = timestampFilename();
                        if (Recorder::start(g_lastFile)) {
                            g_state = AppState::RECORDING;
                            Display::showRecording();
                        }
                    } else if (g_state == AppState::RECORDING) {
                        Recorder::stop();
                        g_state = AppState::UPLOADING;
                        Display::showUploading();
                        // Automatischer Upload direkt im nächsten Loop
                    }
                    break;

                // U: Manuell letzten Upload neu starten
                case 'u':
                case 'U':
                    if (g_state == AppState::IDLE && g_lastFile.length() > 0) {
                        g_state = AppState::UPLOADING;
                        Display::showUploading();
                    }
                    break;

                default:
                    break;
            }
        }
    }

    // ── Aufnahme läuft: Puffer schreiben ─────────────────────────────────────
    if (g_state == AppState::RECORDING) {
        Recorder::tick();
        Display::updateRecordingTime(Recorder::elapsedSeconds());
    }

    // ── Upload auslösen ───────────────────────────────────────────────────────
    if (g_state == AppState::UPLOADING) {
        if (WiFi.status() != WL_CONNECTED) {
            Uploader::addToQueue(g_lastFile);
            Display::showError("Offline, Queue +1");
            g_state = AppState::IDLE;
        } else {
            String jobId = "";
            bool ok = Uploader::upload(g_lastFile, jobId);
            if (ok) {
                g_lastJobId = jobId;
                g_state     = AppState::UPLOAD_OK;
                Display::showUploadOk(jobId);
            } else {
                Uploader::addToQueue(g_lastFile);
                g_state = AppState::UPLOAD_FAIL;
                Display::showError("Upload fehlgesch.");
            }
        }
    }

    // ── Nach 3 Sek. zurück zu IDLE ────────────────────────────────────────────
    if (g_state == AppState::UPLOAD_OK || g_state == AppState::UPLOAD_FAIL) {
        delay(3000);
        g_state = AppState::IDLE;
        Display::showIdle();
    }

    // ── Offline-Queue abarbeiten (wenn WiFi vorhanden) ────────────────────────
    if (g_state == AppState::IDLE && WiFi.status() == WL_CONNECTED) {
        Uploader::processQueue();
    }
}
