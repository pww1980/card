#include <M5Cardputer.h>
#include <SD.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <time.h>

#include "config.h"
#include "recorder.h"
#include "uploader.h"
#include "display.h"

// ── Menüeinträge ─────────────────────────────────────────────────────────────
static const char* MENU_ITEMS[] = {
    "Aufnahme starten",
    "Datei uebermitteln",
    "WLAN einrichten",
    "Server pruefen",
    "SD-Karte pruefen"
};
static const int MENU_COUNT = 5;

// ── Zustandsmaschine ─────────────────────────────────────────────────────────
enum class AppState {
    MENU,
    RECORDING,
    CONFIRM_UPLOAD,
    UPLOADING,
    UPLOAD_OK,
    UPLOAD_FAIL,
    WIFI_SETUP,
    SERVER_CHECK,
    SD_CHECK
};

// ── Globaler Zustand ──────────────────────────────────────────────────────────
static AppState g_state     = AppState::MENU;
static int      g_menuIdx   = 0;
static String   g_lastFile  = "";
static String   g_lastJobId = "";

// WLAN-Setup
static String g_setupSsid  = "";
static String g_setupPass  = "";
static int    g_setupField  = 0;   // 0 = SSID, 1 = Passwort

// Zeitsteuerung für temporäre States
static unsigned long g_stateEnteredMs = 0;

// ── Preferences (NVS) ─────────────────────────────────────────────────────────
static Preferences prefs;

static String loadPref(const char* key, const char* fallback) {
    prefs.begin("dictate", true);
    String val = prefs.getString(key, fallback);
    prefs.end();
    return val;
}

static void savePref(const char* key, const String& val) {
    prefs.begin("dictate", false);
    prefs.putString(key, val);
    prefs.end();
}

// ── Hilfsfunktionen ───────────────────────────────────────────────────────────
static void syncTime() {
    configTime(3600, 3600, "pool.ntp.org", "time.google.com");
}

static String timestampFilename() {
    struct tm ti;
    if (!getLocalTime(&ti, 2000)) {
        return "/rec/rec_" + String(millis()) + ".wav";
    }
    char buf[32];
    strftime(buf, sizeof(buf), "/rec/%Y%m%d_%H%M%S.wav", &ti);
    return String(buf);
}

static void enterState(AppState s) {
    g_state          = s;
    g_stateEnteredMs = millis();
}

static void connectWifi(const String& ssid, const String& pass) {
    Display::showMessage("Verbinde mit WLAN...");
    WiFi.disconnect(true);
    delay(200);
    WiFi.begin(ssid.c_str(), pass.c_str());
    unsigned long t = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t < WIFI_TIMEOUT_MS) {
        delay(200);
    }
    if (WiFi.status() == WL_CONNECTED) {
        syncTime();
        Display::showMessage("Verbunden: " + WiFi.localIP().toString());
        delay(1500);
    } else {
        Display::showError("WLAN-Verbindung fehlgeschlagen");
        delay(2000);
    }
}

// ── Setup ─────────────────────────────────────────────────────────────────────
void setup() {
    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);
    Serial.begin(115200);

    Display::init();
    Display::showMessage("Starte...");

    // SD-Karte
    if (!SD.begin()) {
        Display::showError("SD-Karte fehlt!");
        while (true) delay(1000);
    }
    if (!SD.exists(REC_DIR)) SD.mkdir(REC_DIR);

    // Gespeicherte WLAN-Zugangsdaten laden
    String ssid = loadPref("ssid", WIFI_SSID);
    String pass = loadPref("pass", WIFI_PASS);

    if (ssid.length() > 0) {
        Display::showMessage("WLAN: " + ssid);
        connectWifi(ssid, pass);
    }

    // Letzte bekannte Aufnahmedatei laden
    g_lastFile = loadPref("lastfile", "");

    // Menü anzeigen
    Display::showMenu(MENU_ITEMS, MENU_COUNT, g_menuIdx);
    enterState(AppState::MENU);
}

// ── Tastatur-Helfer ───────────────────────────────────────────────────────────
// Gibt das erste gedrückte Zeichen zurück (0 wenn keins)
static char getKey() {
    if (!M5Cardputer.Keyboard.isChange() || !M5Cardputer.Keyboard.isPressed())
        return 0;
    auto st = M5Cardputer.Keyboard.keysState();
    if (!st.word.empty()) return st.word[0];
    return 0;
}

static bool isEnter() {
    if (!M5Cardputer.Keyboard.isChange() || !M5Cardputer.Keyboard.isPressed())
        return false;
    return M5Cardputer.Keyboard.keysState().enter;
}

static bool isDel() {
    if (!M5Cardputer.Keyboard.isChange() || !M5Cardputer.Keyboard.isPressed())
        return false;
    return M5Cardputer.Keyboard.keysState().del;
}

static bool isTab() {
    if (!M5Cardputer.Keyboard.isChange() || !M5Cardputer.Keyboard.isPressed())
        return false;
    return M5Cardputer.Keyboard.keysState().tab;
}

// ── Haupt-Loop ────────────────────────────────────────────────────────────────
void loop() {
    M5Cardputer.update();

    switch (g_state) {

    // ──────────────────────────────────────────────────────────────────────────
    case AppState::MENU: {
        char c     = getKey();
        bool enter = isEnter();

        // Navigation: W/K = hoch, S/J = runter, 1-4 = direkt
        bool moved = false;
        if (c == 'w' || c == 'W' || c == 'k' || c == 'K') {
            g_menuIdx = (g_menuIdx - 1 + MENU_COUNT) % MENU_COUNT;
            moved = true;
        } else if (c == 's' || c == 'S' || c == 'j' || c == 'J') {
            g_menuIdx = (g_menuIdx + 1) % MENU_COUNT;
            moved = true;
        } else if (c >= '1' && c <= '4') {
            g_menuIdx = c - '1';
            enter = true;   // Direkte Auswahl durch Zifferntaste
        }

        if (moved) {
            Display::showMenu(MENU_ITEMS, MENU_COUNT, g_menuIdx);
        }

        if (enter) {
            switch (g_menuIdx) {

            case 0:  // ── Aufnahme starten ────────────────────────────────────
                g_lastFile = timestampFilename();
                if (Recorder::start(g_lastFile)) {
                    savePref("lastfile", g_lastFile);
                    Display::showRecording(0);
                    enterState(AppState::RECORDING);
                } else {
                    Display::showError("Mikrofon-Fehler");
                    delay(2000);
                    Display::showMenu(MENU_ITEMS, MENU_COUNT, g_menuIdx);
                }
                break;

            case 1:  // ── Datei erneut übermitteln ────────────────────────────
                if (g_lastFile.length() == 0 || !SD.exists(g_lastFile)) {
                    Display::showError("Keine Datei vorhanden");
                    delay(2000);
                    Display::showMenu(MENU_ITEMS, MENU_COUNT, g_menuIdx);
                } else {
                    Display::showUploading();
                    enterState(AppState::UPLOADING);
                }
                break;

            case 2:  // ── WLAN einrichten ──────────────────────────────────────
                g_setupSsid  = loadPref("ssid", WIFI_SSID);
                g_setupPass  = loadPref("pass", WIFI_PASS);
                g_setupField = 0;
                Display::showWifiSetup(g_setupSsid, g_setupPass, g_setupField);
                enterState(AppState::WIFI_SETUP);
                break;

            case 3:  // ── Server prüfen ────────────────────────────────────────
                Display::showServerCheck(SERVER_HOST, SERVER_PORT);
                enterState(AppState::SERVER_CHECK);
                break;

            case 4:  // ── SD-Karte prüfen ──────────────────────────────────────
                enterState(AppState::SD_CHECK);
                break;
            }
        }
        break;
    }

    // ──────────────────────────────────────────────────────────────────────────
    case AppState::RECORDING: {
        // Mikrofon-Puffer in Datei schreiben
        Recorder::tick();

        // Zeit jede Sekunde aktualisieren
        static uint32_t lastSec = 0;
        uint32_t sec = Recorder::elapsedSeconds();
        if (sec != lastSec) {
            lastSec = sec;
            Display::updateRecordingTime(sec);
        }

        // ENTER = Aufnahme stoppen
        if (isEnter()) {
            Recorder::stop();
            Display::showConfirmUpload(g_lastFile);
            enterState(AppState::CONFIRM_UPLOAD);
        }
        break;
    }

    // ──────────────────────────────────────────────────────────────────────────
    case AppState::CONFIRM_UPLOAD: {
        char c = getKey();

        if (c == 'j' || c == 'J' || c == 'y' || c == 'Y') {
            // Ja: übermitteln
            Display::showUploading();
            enterState(AppState::UPLOADING);
        } else if (c == 'n' || c == 'N') {
            // Nein: zurück zum Menü
            Display::showMenu(MENU_ITEMS, MENU_COUNT, g_menuIdx);
            enterState(AppState::MENU);
        }
        break;
    }

    // ──────────────────────────────────────────────────────────────────────────
    case AppState::UPLOADING: {
        // Wird einmalig ausgeführt wenn State betreten wird
        // (State-Enter-Logik: erster Loop-Durchlauf nach State-Wechsel)
        if (millis() - g_stateEnteredMs < 100) {
            // kleines Delay damit Display zeichnen kann
            break;
        }

        if (WiFi.status() != WL_CONNECTED) {
            Uploader::addToQueue(g_lastFile);
            Display::showUploadFail("Kein WLAN – Queue +1");
            enterState(AppState::UPLOAD_FAIL);
            break;
        }

        String jobId = "";
        bool ok = Uploader::upload(g_lastFile, jobId);

        if (ok) {
            g_lastJobId = jobId;
            Display::showUploadOk(jobId);
            enterState(AppState::UPLOAD_OK);
        } else {
            Uploader::addToQueue(g_lastFile);
            Display::showUploadFail("Server-Fehler");
            enterState(AppState::UPLOAD_FAIL);
        }
        break;
    }

    // ──────────────────────────────────────────────────────────────────────────
    case AppState::UPLOAD_OK:
    case AppState::UPLOAD_FAIL: {
        // ENTER oder Timeout (4 Sek.) → zurück zum Menü
        bool timeout = (millis() - g_stateEnteredMs > 4000);
        if (isEnter() || timeout) {
            Display::showMenu(MENU_ITEMS, MENU_COUNT, g_menuIdx);
            enterState(AppState::MENU);
        }
        break;
    }

    // ──────────────────────────────────────────────────────────────────────────
    case AppState::WIFI_SETUP: {
        char c     = getKey();
        bool enter = isEnter();
        bool del   = isDel();
        bool tab   = isTab();

        // ESC-Ersatz: Fn+Q oder einfach 'Q' allein abfangen
        if (c == 27 /* ESC */ ) {
            Display::showMenu(MENU_ITEMS, MENU_COUNT, g_menuIdx);
            enterState(AppState::MENU);
            break;
        }

        bool redraw = false;

        // TAB: Feld wechseln
        if (tab) {
            g_setupField = 1 - g_setupField;
            redraw = true;
        }

        // Backspace: letztes Zeichen löschen
        if (del) {
            if (g_setupField == 0 && g_setupSsid.length() > 0) {
                g_setupSsid.remove(g_setupSsid.length() - 1);
                redraw = true;
            } else if (g_setupField == 1 && g_setupPass.length() > 0) {
                g_setupPass.remove(g_setupPass.length() - 1);
                redraw = true;
            }
        }

        // Druckbares Zeichen eingeben (max. Feldlänge begrenzen)
        if (c >= 0x20 && c <= 0x7E) {
            if (g_setupField == 0 && g_setupSsid.length() < 32) {
                g_setupSsid += c;
                redraw = true;
            } else if (g_setupField == 1 && g_setupPass.length() < 64) {
                g_setupPass += c;
                redraw = true;
            }
        }

        // ENTER: Verbinden
        if (enter) {
            if (g_setupField == 0) {
                // Erst zu Passwort-Feld wechseln, wenn SSID noch aktiv
                g_setupField = 1;
                redraw = true;
            } else {
                // Verbinden und Zugangsdaten speichern
                savePref("ssid", g_setupSsid);
                savePref("pass", g_setupPass);
                connectWifi(g_setupSsid, g_setupPass);
                Display::showMenu(MENU_ITEMS, MENU_COUNT, g_menuIdx);
                enterState(AppState::MENU);
                break;
            }
        }

        if (redraw) {
            Display::showWifiSetup(g_setupSsid, g_setupPass, g_setupField);
        }
        break;
    }

    // ──────────────────────────────────────────────────────────────────────────
    case AppState::SERVER_CHECK: {
        // Einmalig ausführen direkt nach State-Eintritt
        static bool checked = false;
        if (!checked && millis() - g_stateEnteredMs > 150) {
            checked = true;

            if (WiFi.status() != WL_CONNECTED) {
                Display::showServerResult(false, "Kein WLAN");
            } else {
                HTTPClient http;
                String url = "http://" + String(SERVER_HOST) + ":" +
                             String(SERVER_PORT) + "/health";
                http.begin(url);
                http.setTimeout(5000);
                int code = http.GET();

                if (code == 200) {
                    String body = http.getString();
                    Display::showServerResult(true, "HTTP 200 – " + body.substring(0, 28));
                } else if (code > 0) {
                    Display::showServerResult(false, "HTTP " + String(code));
                } else {
                    Display::showServerResult(false, "Keine Verbindung");
                }
                http.end();
            }
        }

        // Warten auf ENTER → zurück zum Menü
        if (isEnter()) {
            checked = false;  // Reset für nächsten Aufruf
            Display::showMenu(MENU_ITEMS, MENU_COUNT, g_menuIdx);
            enterState(AppState::MENU);
        }
        break;
    }

    // ──────────────────────────────────────────────────────────────────────────
    case AppState::SD_CHECK: {
        static bool sdChecked = false;
        if (!sdChecked && millis() - g_stateEnteredMs > 150) {
            sdChecked = true;

            // SD neu initialisieren um aktuellen Zustand zu prüfen
            if (!SD.begin()) {
                Display::showSdResult(false, "", 0, 0, 0);
            } else {
                // Kartentyp
                String cardType;
                switch (SD.cardType()) {
                    case CARD_MMC:  cardType = "MMC";   break;
                    case CARD_SD:   cardType = "SD";    break;
                    case CARD_SDHC: cardType = "SDHC";  break;
                    default:        cardType = "Unbekannt"; break;
                }

                uint64_t totalMB = SD.totalBytes() / (1024 * 1024);
                uint64_t usedMB  = SD.usedBytes()  / (1024 * 1024);

                // Dateien in /rec zählen
                int recFiles = 0;
                File dir = SD.open(REC_DIR);
                if (dir) {
                    File f = dir.openNextFile();
                    while (f) {
                        if (!f.isDirectory()) recFiles++;
                        f.close();
                        f = dir.openNextFile();
                    }
                    dir.close();
                }

                Display::showSdResult(true, cardType, totalMB, usedMB, recFiles);
            }
        }

        if (isEnter()) {
            sdChecked = false;
            Display::showMenu(MENU_ITEMS, MENU_COUNT, g_menuIdx);
            enterState(AppState::MENU);
        }
        break;
    }

    } // switch

    // ── Offline-Queue im Hintergrund (nur im MENU-State) ─────────────────────
    static unsigned long lastQueueMs = 0;
    if (g_state == AppState::MENU &&
        WiFi.status() == WL_CONNECTED &&
        millis() - lastQueueMs > 30000) {
        lastQueueMs = millis();
        Uploader::processQueue();
    }
}
