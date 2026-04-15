#include <M5Cardputer.h>
#include <SD.h>
#include <SPI.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <time.h>
#include <vector>

#include "config.h"
#include "recorder.h"
#include "uploader.h"
#include "display.h"

// ── Menüeinträge ─────────────────────────────────────────────────────────────
static const char* MENU_ITEMS[] = {
    "Aufnahme starten",
    "Aufnahmen ansehen",
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
    JOB_POLL,
    FILE_LIST,
    CAPTIVE_PORTAL,  // WLAN + Server über Webseite einrichten
    SERVER_CHECK,
    SD_CHECK
};

// ── Globaler Zustand ──────────────────────────────────────────────────────────
static AppState  g_state     = AppState::MENU;
static int       g_menuIdx   = 0;
static String    g_lastFile  = "";
static String    g_lastJobId = "";
static unsigned long g_stateEnteredMs = 0;

// WLAN + Server (laufzeit-konfigurierbar, aus NVS geladen)
static String g_setupSsid   = "";
static String g_setupPass   = "";
static String g_serverHost  = SERVER_HOST;
static int    g_serverPort  = SERVER_PORT;

// Captive Portal (WiFiManager – blockierend, daher kein State nötig)
static bool g_portalActive = false;

// File-Browser
static std::vector<RecFileEntry> g_fileList;
static int g_fileListIdx    = 0;
static int g_fileListOffset = 0;

// Job-Polling
static unsigned long g_lastPollMs    = 0;
static String        g_pollStatus    = "queued";
static const int     POLL_INTERVAL_MS = 3000;

// Batterie (gecacht, alle 10s aktualisiert)
static int  g_battPct    = -1;
static bool g_charging   = false;
static unsigned long g_lastBattMs = 0;

// ── Preferences ───────────────────────────────────────────────────────────────
static Preferences prefs;

static String loadPref(const char* key, const char* fallback) {
    prefs.begin("dictate", true);
    String v = prefs.getString(key, fallback);
    prefs.end();
    return v;
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
    if (!getLocalTime(&ti, 2000))
        return "/rec/rec_" + String(millis()) + ".wav";
    char buf[32];
    strftime(buf, sizeof(buf), "/rec/%Y%m%d_%H%M%S.wav", &ti);
    return String(buf);
}

static void enterState(AppState s) {
    g_state          = s;
    g_stateEnteredMs = millis();
}

static void connectWifi(const String& ssid, const String& pass) {
    Display::showMessage("Verbinde: " + ssid);
    WiFi.disconnect(true);
    delay(200);
    WiFi.begin(ssid.c_str(), pass.c_str());
    unsigned long t = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t < WIFI_TIMEOUT_MS)
        delay(200);
    if (WiFi.status() == WL_CONNECTED) {
        syncTime();
        Display::showMessage("Verbunden: " + WiFi.localIP().toString());
        delay(1500);
    } else {
        Display::showError("WLAN-Verbindung fehlgeschlagen");
        delay(2000);
    }
}

static void updateBattery() {
    if (millis() - g_lastBattMs < 10000) return;
    g_lastBattMs = millis();
    g_battPct    = M5Cardputer.Power.getBatteryLevel();
    g_charging   = M5Cardputer.Power.isCharging();
}

// Dateiliste aus /rec neu laden (nach Datum absteigend sortiert)
static void loadFileList() {
    g_fileList.clear();
    File dir = SD.open(REC_DIR);
    if (!dir) return;
    File f = dir.openNextFile();
    while (f) {
        if (!f.isDirectory() && String(f.name()).endsWith(".wav")) {
            RecFileEntry e;
            e.name   = String(f.name());  // nur Dateiname, kein Pfad
            e.sizeKB = (uint32_t)(f.size() / 1024);
            g_fileList.push_back(e);
        }
        f.close();
        f = dir.openNextFile();
    }
    dir.close();
    // Neueste zuerst (Dateinamen sind Timestamps → lexikografisch absteigend)
    std::sort(g_fileList.begin(), g_fileList.end(),
              [](const RecFileEntry& a, const RecFileEntry& b) {
                  return a.name > b.name;
              });
}

// Vollständigen Pfad für Dateilisteneintrag liefern
static String fullPath(const RecFileEntry& e) {
    return String(REC_DIR) + "/" + e.name;
}

// HTTP GET /status/{job_id}
static String pollJobStatus(const String& jobId) {
    if (WiFi.status() != WL_CONNECTED) return "offline";
    WiFiClient client;
    if (!client.connect(g_serverHost.c_str(), g_serverPort)) return "no_conn";
    client.printf("GET /status/%s HTTP/1.1\r\nHost: %s:%d\r\nConnection: close\r\n\r\n",
                  jobId.c_str(), g_serverHost.c_str(), g_serverPort);
    unsigned long dl = millis() + 5000;
    while (client.available() == 0 && millis() < dl) delay(10);
    String status_line = client.readStringUntil('\n');
    int code = (status_line.length() > 12) ? status_line.substring(9,12).toInt() : 0;
    if (code != 200) { client.stop(); return "http_" + String(code); }
    while (client.available()) {
        String line = client.readStringUntil('\n');
        if (line == "\r" || line.isEmpty()) break;
    }
    String body;
    while (client.available()) body += (char)client.read();
    client.stop();
    JsonDocument doc;
    if (deserializeJson(doc, body) == DeserializationError::Ok)
        return doc["status"].as<String>();
    return "parse_err";
}

// ── SD-Initialisierung ────────────────────────────────────────────────────────
// Expliziter SPI-Bus nötig, da M5Cardputer SPI nicht automatisch auf
// den richtigen Pins (SCK=40, MISO=39, MOSI=14, CS=12) konfiguriert.
static bool initSD() {
    SPI.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
    delay(10);
    if (SD.begin(SD_CS_PIN, SPI, 25000000)) return true;
    delay(200);
    // Fallback: halbe Taktrate
    return SD.begin(SD_CS_PIN, SPI, 4000000);
}

// ── Setup ─────────────────────────────────────────────────────────────────────
void setup() {
    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);
    Serial.begin(115200);

    Display::init();
    Display::showMessage("Starte...");

    if (!initSD()) {
        Display::showError("SD-Karte fehlt!");
        while (true) delay(1000);
    }
    if (!SD.exists(REC_DIR)) SD.mkdir(REC_DIR);

    // Batterie initialisieren
    g_battPct  = M5Cardputer.Power.getBatteryLevel();
    g_charging = M5Cardputer.Power.isCharging();
    g_lastBattMs = millis();

    // Server-Konfiguration aus NVS laden
    g_serverHost = loadPref("host", SERVER_HOST);
    {
        String portStr = loadPref("port", String(SERVER_PORT).c_str());
        int p = portStr.toInt();
        g_serverPort = (p > 0) ? p : SERVER_PORT;
    }
    Uploader::setServer(g_serverHost, g_serverPort);

    // WLAN
    String ssid = loadPref("ssid", WIFI_SSID);
    String pass = loadPref("pass", WIFI_PASS);
    if (ssid.length() > 0) {
        Display::showMessage("WLAN: " + ssid);
        connectWifi(ssid, pass);
    }

    g_lastFile = loadPref("lastfile", "");

    Display::showMenu(MENU_ITEMS, MENU_COUNT, g_menuIdx, g_battPct, g_charging);
    enterState(AppState::MENU);
}

// ── Tastatur-Helfer ───────────────────────────────────────────────────────────
// isChange() löscht intern das Flag beim ersten Aufruf → einmalig pro Loop
// alles aus keysState() lesen und im Struct cachen.
struct KeyEvent {
    char ch    = 0;
    bool enter = false;
    bool del   = false;
    bool tab   = false;
    bool fn    = false;
};
static KeyEvent g_key;

static void captureKeys() {
    g_key = {};
    if (!M5Cardputer.Keyboard.isChange() || !M5Cardputer.Keyboard.isPressed())
        return;
    auto st     = M5Cardputer.Keyboard.keysState();
    g_key.enter = st.enter;
    g_key.del   = st.del;
    g_key.tab   = st.tab;
    g_key.fn    = st.fn;
    if (!st.word.empty()) g_key.ch = st.word[0];
}

// ── Loop ──────────────────────────────────────────────────────────────────────
void loop() {
    M5Cardputer.update();
    captureKeys();   // einmalig – isChange() darf nur einmal aufgerufen werden

    switch (g_state) {

    // ── MENU ─────────────────────────────────────────────────────────────────
    case AppState::MENU: {
        updateBattery();
        char c     = g_key.ch;
        bool enter = g_key.enter;
        bool moved = false;

        if      (c == 'a' || c == 'A' || c == 'w' || c == 'W')
            { g_menuIdx = (g_menuIdx - 1 + MENU_COUNT) % MENU_COUNT; moved = true; }
        else if (c == 'd' || c == 'D' || c == 's' || c == 'S')
            { g_menuIdx = (g_menuIdx + 1) % MENU_COUNT; moved = true; }
        else if (c >= '1' && c <= '5')
            { g_menuIdx = c - '1'; enter = true; }

        if (moved)
            Display::showMenu(MENU_ITEMS, MENU_COUNT, g_menuIdx, g_battPct, g_charging);

        if (enter) {
            switch (g_menuIdx) {

            case 0:  // ── Aufnahme starten ──────────────────────────────────
                g_lastFile = timestampFilename();
                if (Recorder::start(g_lastFile)) {
                    savePref("lastfile", g_lastFile);
                    Display::showRecording(0, Recorder::getGain(), 0);
                    enterState(AppState::RECORDING);
                } else {
                    Display::showError("Mikrofon-Fehler");
                    delay(2000);
                    Display::showMenu(MENU_ITEMS, MENU_COUNT, g_menuIdx,
                                      g_battPct, g_charging);
                }
                break;

            case 1:  // ── Aufnahmen ansehen ─────────────────────────────────
                loadFileList();
                g_fileListIdx    = 0;
                g_fileListOffset = 0;
                Display::showFileList(g_fileList, g_fileListIdx, g_fileListOffset);
                enterState(AppState::FILE_LIST);
                break;

            case 2:  // ── WLAN einrichten ───────────────────────────────────
                enterState(AppState::CAPTIVE_PORTAL);
                break;

            case 3:  // ── Server prüfen ─────────────────────────────────────
                Display::showServerCheck(g_serverHost, g_serverPort);
                enterState(AppState::SERVER_CHECK);
                break;

            case 4:  // ── SD-Karte prüfen ───────────────────────────────────
                enterState(AppState::SD_CHECK);
                break;
            }
        }
        break;
    }

    // ── RECORDING ────────────────────────────────────────────────────────────
    case AppState::RECORDING: {
        Recorder::tick();

        static uint32_t lastSec = 0;
        uint32_t sec = Recorder::elapsedSeconds();
        if (sec != lastSec) {
            lastSec = sec;
            Display::updateRecording(sec, Recorder::getGain(), Recorder::getLevel());
        }

        char c = g_key.ch;

        // +/- Gain live anpassen
        if (c == '+' || c == '=') {
            Recorder::setGain(Recorder::getGain() + 1);
            Display::updateRecording(sec, Recorder::getGain(), Recorder::getLevel());
        } else if (c == '-' || c == '_') {
            Recorder::setGain(Recorder::getGain() - 1);
            Display::updateRecording(sec, Recorder::getGain(), Recorder::getLevel());
        }

        if (g_key.enter) {
            Recorder::stop();
            Display::showConfirmUpload(g_lastFile);
            enterState(AppState::CONFIRM_UPLOAD);
        }
        break;
    }

    // ── CONFIRM UPLOAD ───────────────────────────────────────────────────────
    case AppState::CONFIRM_UPLOAD: {
        char c = g_key.ch;
        if (c == 'j' || c == 'J' || c == 'y' || c == 'Y') {
            Display::showUploading();
            enterState(AppState::UPLOADING);
        } else if (c == 'n' || c == 'N') {
            Display::showMenu(MENU_ITEMS, MENU_COUNT, g_menuIdx, g_battPct, g_charging);
            enterState(AppState::MENU);
        }
        break;
    }

    // ── UPLOADING ────────────────────────────────────────────────────────────
    case AppState::UPLOADING: {
        if (millis() - g_stateEnteredMs < 150) break;  // Display zeichnen lassen

        if (WiFi.status() != WL_CONNECTED) {
            Uploader::addToQueue(g_lastFile);
            Display::showUploadFail("Kein WLAN – Queue +1");
            enterState(AppState::UPLOAD_FAIL);
            break;
        }

        String jobId;
        bool ok = Uploader::upload(g_lastFile, jobId);

        if (ok) {
            g_lastJobId  = jobId;
            g_pollStatus = "queued";
            g_lastPollMs = 0;
            Display::showUploadOk(jobId);
            delay(1500);
            // Header für Polling zeichnen
            M5Cardputer.Display.fillRect(0, 0, 240, 22, 0xC5E0);
            M5Cardputer.Display.setTextColor(TFT_WHITE, 0xC5E0);
            M5Cardputer.Display.setTextSize(1);
            M5Cardputer.Display.setCursor(6, 7);
            M5Cardputer.Display.print("Verarbeitung...");
            enterState(AppState::JOB_POLL);
        } else {
            Uploader::addToQueue(g_lastFile);
            Display::showUploadFail("Server-Fehler");
            enterState(AppState::UPLOAD_FAIL);
        }
        break;
    }

    // ── JOB_POLL ─────────────────────────────────────────────────────────────
    case AppState::JOB_POLL: {
        uint32_t elapsed = (millis() - g_stateEnteredMs) / 1000;

        // Alle POLL_INTERVAL_MS abfragen
        if (millis() - g_lastPollMs > POLL_INTERVAL_MS) {
            g_lastPollMs = millis();
            g_pollStatus = pollJobStatus(g_lastJobId);
        }

        Display::showJobPoll(g_lastJobId, g_pollStatus, elapsed);

        // Fertig oder Fehler → Ergebnis-Screen
        if (g_pollStatus == "done") {
            delay(800);
            Display::showUploadOk(g_lastJobId);
            enterState(AppState::UPLOAD_OK);
        } else if (g_pollStatus == "failed") {
            Display::showUploadFail("Pipeline-Fehler auf Server");
            enterState(AppState::UPLOAD_FAIL);
        }

        // ENTER → sofort zurück zum Menü (Job läuft auf Server weiter)
        if (g_key.enter) {
            Display::showMenu(MENU_ITEMS, MENU_COUNT, g_menuIdx, g_battPct, g_charging);
            enterState(AppState::MENU);
        }
        break;
    }

    // ── UPLOAD_OK / UPLOAD_FAIL ───────────────────────────────────────────────
    case AppState::UPLOAD_OK:
    case AppState::UPLOAD_FAIL: {
        bool timeout = (millis() - g_stateEnteredMs > 4000);
        if (g_key.enter || timeout) {
            Display::showMenu(MENU_ITEMS, MENU_COUNT, g_menuIdx, g_battPct, g_charging);
            enterState(AppState::MENU);
        }
        break;
    }

    // ── FILE_LIST ────────────────────────────────────────────────────────────
    case AppState::FILE_LIST: {
        char c     = g_key.ch;
        bool enter = g_key.enter;
        bool back  = false;
        bool moved = false;

        const int VISIBLE = 4;

        if (c == 'w' || c == 'W' || c == 'k' || c == 'K') {
            if (g_fileListIdx > 0) {
                g_fileListIdx--;
                if (g_fileListIdx < g_fileListOffset)
                    g_fileListOffset = g_fileListIdx;
                moved = true;
            }
        } else if (c == 's' || c == 'S' || c == 'j' || c == 'J') {
            if (g_fileListIdx < (int)g_fileList.size() - 1) {
                g_fileListIdx++;
                if (g_fileListIdx >= g_fileListOffset + VISIBLE)
                    g_fileListOffset = g_fileListIdx - VISIBLE + 1;
                moved = true;
            }
        } else if (c == 'n' || c == 'N' || c == 27 /* ESC */ ) {
            back = true;
        }

        if (moved)
            Display::showFileList(g_fileList, g_fileListIdx, g_fileListOffset);

        if (enter && !g_fileList.empty()) {
            g_lastFile = fullPath(g_fileList[g_fileListIdx]);
            Display::showConfirmUpload(g_lastFile);
            enterState(AppState::CONFIRM_UPLOAD);
        }

        if (back) {
            Display::showMenu(MENU_ITEMS, MENU_COUNT, g_menuIdx, g_battPct, g_charging);
            enterState(AppState::MENU);
        }
        break;
    }

    // ── CAPTIVE_PORTAL ───────────────────────────────────────────────────────
    // WiFiManager läuft blockierend – Display anzeigen, dann Portal starten.
    case AppState::CAPTIVE_PORTAL: {
        Display::showCaptivePortal("DiktatSetup", "192.168.4.1");

        WiFiManager wm;
        wm.setConnectTimeout(15);          // 15s Verbindungsversuch nach Submit
        wm.setConfigPortalTimeout(300);    // 5 min Portal-Timeout

        // Eigene Parameter: Server-IP und -Port
        WiFiManagerParameter hostParam("host", "Server-IP",
                                       g_serverHost.c_str(), 40);
        WiFiManagerParameter portParam("port", "Server-Port",
                                       String(g_serverPort).c_str(), 8);
        wm.addParameter(&hostParam);
        wm.addParameter(&portParam);

        bool ok = wm.startConfigPortal("DiktatSetup");

        // Server-Parameter immer übernehmen – auch wenn WLAN-Connect fehlschlug
        {
            String host = String(hostParam.getValue());
            int    port = String(portParam.getValue()).toInt();
            if (host.length() > 0) { g_serverHost = host; savePref("host", host); }
            if (port > 0)          { g_serverPort = port; savePref("port", String(port)); }
            Uploader::setServer(g_serverHost, g_serverPort);
        }

        if (ok) {
            // WiFi-Zugangsdaten in eigener NVS-Partition sichern
            savePref("ssid", WiFi.SSID());
            savePref("pass", WiFi.psk());
            syncTime();
        }

        Display::showMenu(MENU_ITEMS, MENU_COUNT, g_menuIdx, g_battPct, g_charging);
        enterState(AppState::MENU);
        break;
    }

    // ── SERVER_CHECK ─────────────────────────────────────────────────────────
    case AppState::SERVER_CHECK: {
        static bool checked = false;
        if (!checked && millis() - g_stateEnteredMs > 150) {
            checked = true;
            if (WiFi.status() != WL_CONNECTED) {
                Display::showServerResult(false, "Kein WLAN");
            } else {
                HTTPClient http;
                String url = "http://" + g_serverHost + ":" +
                             String(g_serverPort) + "/health";
                http.begin(url);
                http.setTimeout(5000);
                int code = http.GET();
                if (code == 200)
                    Display::showServerResult(true,
                        "HTTP 200  " + http.getString().substring(0, 28));
                else if (code > 0)
                    Display::showServerResult(false, "HTTP " + String(code));
                else
                    Display::showServerResult(false, "Keine Verbindung");
                http.end();
            }
        }
        if (g_key.enter) {
            checked = false;
            Display::showMenu(MENU_ITEMS, MENU_COUNT, g_menuIdx, g_battPct, g_charging);
            enterState(AppState::MENU);
        }
        break;
    }

    // ── SD_CHECK ─────────────────────────────────────────────────────────────
    case AppState::SD_CHECK: {
        static bool sdChecked = false;
        if (!sdChecked && millis() - g_stateEnteredMs > 150) {
            sdChecked = true;
            // SD ist bereits in setup() gemountet – nur Infos abfragen
            sdcard_type_t ct = SD.cardType();
            if (ct == CARD_NONE || ct == CARD_UNKNOWN) {
                Display::showSdResult(false, "", 0, 0, 0);
            } else {
                String cardType;
                switch (ct) {
                    case CARD_MMC:  cardType = "MMC";  break;
                    case CARD_SD:   cardType = "SD";   break;
                    case CARD_SDHC: cardType = "SDHC"; break;
                    default:        cardType = "Unbekannt"; break;
                }
                uint64_t totalMB = SD.totalBytes() / (1024*1024);
                uint64_t usedMB  = SD.usedBytes()  / (1024*1024);
                int recFiles = 0;
                File dir = SD.open(REC_DIR);
                if (dir) {
                    File f = dir.openNextFile();
                    while (f) { if (!f.isDirectory()) recFiles++; f.close(); f = dir.openNextFile(); }
                    dir.close();
                }
                Display::showSdResult(true, cardType, totalMB, usedMB, recFiles);
            }
        }
        if (g_key.enter) {
            sdChecked = false;
            Display::showMenu(MENU_ITEMS, MENU_COUNT, g_menuIdx, g_battPct, g_charging);
            enterState(AppState::MENU);
        }
        break;
    }

    } // switch

    // ── Offline-Queue im Hintergrund ─────────────────────────────────────────
    static unsigned long lastQueueMs = 0;
    if (g_state == AppState::MENU &&
        WiFi.status() == WL_CONNECTED &&
        millis() - lastQueueMs > 30000) {
        lastQueueMs = millis();
        Uploader::processQueue();
    }
}
