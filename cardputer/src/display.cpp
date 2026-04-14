#include "display.h"
#include "config.h"
#include <M5Cardputer.h>

// ── Farbpalette ───────────────────────────────────────────────────────────────
static const uint32_t C_BG       = TFT_BLACK;
static const uint32_t C_HDR_IDLE = 0x2945;  // Dunkelblaugrau
static const uint32_t C_HDR_REC  = TFT_RED;
static const uint32_t C_HDR_OK   = 0x2724;  // Dunkelgrün
static const uint32_t C_HDR_WARN = 0xC600;  // Orange
static const uint32_t C_SEL_BG   = 0x0319;  // Auswahlbalken (dunkles Blau)
static const uint32_t C_SEL_TXT  = TFT_WHITE;
static const uint32_t C_DIM      = 0x8410;  // Gedimmt (grau)
static const uint32_t C_HINT     = 0x528A;  // Hinweistext

// Display-Dimensionen
static const int W = 240;
static const int H = 135;

// ── Hilfsfunktionen ───────────────────────────────────────────────────────────
static auto& D() { return M5Cardputer.Display; }

static void clear() {
    D().fillScreen(C_BG);
}

static void drawHeader(const char* title, uint32_t color) {
    D().fillRect(0, 0, W, 22, color);
    D().setTextColor(TFT_WHITE, color);
    D().setTextSize(1);
    D().setCursor(6, 7);
    D().print(title);
    D().setTextColor(TFT_WHITE, C_BG);
}

static void drawFooter(const char* hint) {
    D().fillRect(0, H - 16, W, 16, 0x2104);  // sehr dunkles grau
    D().setTextColor(C_HINT, 0x2104);
    D().setTextSize(1);
    D().setCursor(6, H - 11);
    D().print(hint);
    D().setTextColor(TFT_WHITE, C_BG);
}

static void drawInputField(int y, const char* label,
                            const String& value, bool active) {
    uint32_t border = active ? TFT_CYAN : C_DIM;
    // Rahmen
    D().drawRect(6, y, W - 12, 22, border);
    // Label links im Rahmen
    D().setTextColor(active ? TFT_CYAN : C_DIM, C_BG);
    D().setTextSize(1);
    D().setCursor(10, y + 3);
    D().print(label);
    // Wert
    D().setTextColor(TFT_WHITE, C_BG);
    D().setCursor(10 + strlen(label) * 6 + 4, y + 3);
    D().fillRect(10 + strlen(label) * 6 + 4, y + 1,
                 W - 12 - 10 - strlen(label) * 6 - 4, 20, C_BG);
    // Cursor-Indikator wenn aktiv
    String display_val = value;
    if (active) display_val += "_";
    D().print(display_val);
}

// ── Öffentliche Funktionen ────────────────────────────────────────────────────

void Display::init() {
    D().setRotation(1);
    D().setTextSize(1);
    D().setTextColor(TFT_WHITE, C_BG);
    clear();
}

void Display::showMenu(const char* const items[], int count, int selected) {
    clear();
    drawHeader("Diktiergeraet", C_HDR_IDLE);

    const int ITEM_H   = 24;
    const int START_Y  = 24;

    for (int i = 0; i < count; i++) {
        int y   = START_Y + i * ITEM_H;
        bool sel = (i == selected);

        // Hintergrund
        D().fillRect(0, y, W, ITEM_H, sel ? C_SEL_BG : C_BG);

        // Auswahl-Markierung
        if (sel) {
            D().fillRect(0, y, 4, ITEM_H, TFT_CYAN);
        }

        // Nummerierung + Text
        D().setTextColor(sel ? C_SEL_TXT : C_DIM, sel ? C_SEL_BG : C_BG);
        D().setTextSize(1);
        D().setCursor(10, y + 8);
        D().printf("%d. %s", i + 1, items[i]);
    }

    // Footer
    drawFooter("W/S Navigieren   ENTER Auswaehlen");
    D().setTextColor(TFT_WHITE, C_BG);
}

void Display::showRecording(uint32_t seconds) {
    clear();

    // Header mit blinkendem Punkt (wird durch updateRecordingTime aktualisiert)
    drawHeader("  RECORDING", C_HDR_REC);

    // Zeitanzeige groß und zentriert
    char buf[8];
    snprintf(buf, sizeof(buf), "%02lu:%02lu", seconds / 60, seconds % 60);

    D().setTextSize(3);
    D().setTextColor(TFT_WHITE, C_BG);
    // Horizontale Zentrierung (3*6px = 18px pro Zeichen bei Größe 3)
    int text_w = strlen(buf) * 18;
    D().setCursor((W - text_w) / 2, 50);
    D().print(buf);
    D().setTextSize(1);

    drawFooter("ENTER = Aufnahme stoppen");
    D().setTextColor(TFT_WHITE, C_BG);
}

void Display::updateRecordingTime(uint32_t seconds) {
    // Nur das Zeitfeld neu zeichnen – kein clear() um Flimmern zu vermeiden
    char buf[8];
    snprintf(buf, sizeof(buf), "%02lu:%02lu", seconds / 60, seconds % 60);

    D().setTextSize(3);

    // Blinkendes Recording-Symbol (jede Sekunde)
    bool blink = (seconds % 2 == 0);
    D().fillRect(0, 0, 22, 22, C_HDR_REC);  // Header-Bereich
    D().setTextColor(blink ? TFT_WHITE : C_HDR_REC, C_HDR_REC);
    D().setTextSize(1);
    D().setCursor(6, 7);
    D().print("●");
    D().setTextColor(TFT_WHITE, C_HDR_REC);
    D().setCursor(18, 7);
    D().print(" RECORDING");

    // Zeit aktualisieren
    D().setTextColor(TFT_WHITE, C_BG);
    D().setTextSize(3);
    int text_w = strlen(buf) * 18;
    D().fillRect(0, 45, W, 30, C_BG);
    D().setCursor((W - text_w) / 2, 50);
    D().print(buf);
    D().setTextSize(1);
}

void Display::showConfirmUpload(const String& filename) {
    clear();
    drawHeader("Aufnahme beendet", C_HDR_WARN);

    D().setTextSize(1);
    D().setTextColor(TFT_WHITE, C_BG);
    D().setCursor(6, 32);
    D().print("Datei uebermitteln?");

    D().setTextColor(C_DIM, C_BG);
    D().setCursor(6, 48);
    // Nur Dateiname (ohne Pfad) anzeigen
    int slash = filename.lastIndexOf('/');
    D().print(filename.substring(slash + 1));

    // Ja/Nein Buttons
    D().fillRect(10,  75, 90, 28, 0x2724);  // Grün für Ja
    D().fillRect(140, 75, 90, 28, 0x6000);  // Rot für Nein

    D().setTextColor(TFT_WHITE, 0x2724);
    D().setTextSize(1);
    D().setCursor(38, 85);
    D().print("[J] Ja");

    D().setTextColor(TFT_WHITE, 0x6000);
    D().setCursor(161, 85);
    D().print("[N] Nein");

    D().setTextColor(TFT_WHITE, C_BG);
}

void Display::showUploading() {
    clear();
    drawHeader("Uebertrage...", 0x0319);
    D().setTextSize(1);
    D().setTextColor(TFT_CYAN, C_BG);
    D().setCursor(6, 38);
    D().print("Sende Datei an Server...");
    // Einfacher Fortschrittsbalken (animiert in main.cpp durch Neuzeichnen)
    D().drawRect(10, 60, W - 20, 12, C_DIM);
    D().setTextColor(C_DIM, C_BG);
    D().setCursor(6, 80);
    D().print("Bitte warten.");
}

void Display::showUploadOk(const String& jobId) {
    clear();
    drawHeader("Erfolgreich!", C_HDR_OK);

    D().setTextSize(1);
    D().setTextColor(TFT_GREEN, C_BG);
    D().setCursor(6, 35);
    D().print("Datei uebermittelt");

    D().setTextColor(C_DIM, C_BG);
    D().setCursor(6, 53);
    D().print("Job-ID:");
    D().setTextColor(TFT_WHITE, C_BG);
    D().setCursor(50, 53);
    D().print(jobId.length() > 0 ? jobId : "---");

    drawFooter("Weiter mit ENTER");
    D().setTextColor(TFT_WHITE, C_BG);
}

void Display::showUploadFail(const String& reason) {
    clear();
    drawHeader("Fehler!", C_HDR_REC);

    D().setTextSize(1);
    D().setTextColor(TFT_RED, C_BG);
    D().setCursor(6, 35);
    D().print("Uebertragung fehlgeschl.");

    D().setTextColor(C_DIM, C_BG);
    D().setCursor(6, 53);
    D().print(reason.substring(0, 36));

    D().setTextColor(TFT_YELLOW, C_BG);
    D().setCursor(6, 71);
    D().print("Datei in Queue gespeichert.");

    drawFooter("Weiter mit ENTER");
    D().setTextColor(TFT_WHITE, C_BG);
}

void Display::showWifiSetup(const String& ssid, const String& pass, int field) {
    clear();
    drawHeader("WLAN einrichten", C_HDR_IDLE);

    D().setTextSize(1);
    D().setTextColor(C_DIM, C_BG);
    D().setCursor(6, 27);
    D().print("Tastatur: Text eingeben, DEL=loeschen");

    drawInputField(42,  "SSID: ", ssid, field == 0);
    drawInputField(72,  "Pass: ", String("*").length() > 0
                        ? String(pass.length(), '*')
                        : pass, field == 1);

    drawFooter("TAB=Feld wechseln  ENTER=Verbinden  ESC=Zurueck");
    D().setTextColor(TFT_WHITE, C_BG);
}

void Display::showServerCheck(const String& host, int port) {
    clear();
    drawHeader("Server pruefen", C_HDR_IDLE);

    D().setTextSize(1);
    D().setTextColor(TFT_WHITE, C_BG);
    D().setCursor(6, 32);
    D().printf("Host: %s:%d", host.c_str(), port);

    D().setTextColor(TFT_CYAN, C_BG);
    D().setCursor(6, 52);
    D().print("Verbinde...");
}

void Display::showServerResult(bool ok, const String& detail) {
    // Nur das Ergebnis aktualisieren, Header bleibt
    D().fillRect(0, 50, W, 65, C_BG);

    D().setTextSize(1);
    if (ok) {
        D().setTextColor(TFT_GREEN, C_BG);
        D().setCursor(6, 52);
        D().print("Server erreichbar");
        D().setTextColor(C_DIM, C_BG);
        D().setCursor(6, 68);
        D().print(detail.substring(0, 38));
    } else {
        D().setTextColor(TFT_RED, C_BG);
        D().setCursor(6, 52);
        D().print("Nicht erreichbar!");
        D().setTextColor(C_DIM, C_BG);
        D().setCursor(6, 68);
        D().print(detail.substring(0, 38));
    }

    drawFooter("ENTER = Zurueck");
    D().setTextColor(TFT_WHITE, C_BG);
}

void Display::showMessage(const String& msg) {
    clear();
    D().setTextSize(1);
    D().setTextColor(TFT_CYAN, C_BG);
    D().setCursor(6, 55);
    D().print(msg);
    D().setTextColor(TFT_WHITE, C_BG);
}

void Display::showError(const String& msg) {
    clear();
    drawHeader("FEHLER", C_HDR_REC);
    D().setTextSize(1);
    D().setTextColor(TFT_RED, C_BG);
    D().setCursor(6, 35);
    D().print(msg);
    D().setTextColor(TFT_WHITE, C_BG);
}
