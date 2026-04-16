#include "display.h"
#include "config.h"
#include <M5Cardputer.h>

// ── Farbpalette ───────────────────────────────────────────────────────────────
static const uint32_t C_BG       = TFT_BLACK;
static const uint32_t C_HDR_IDLE = 0x2945;
static const uint32_t C_HDR_REC  = TFT_RED;
static const uint32_t C_HDR_OK   = 0x2724;
static const uint32_t C_HDR_WARN = 0xC600;
static const uint32_t C_HDR_POLL = 0xC5E0;  // Goldgelb
static const uint32_t C_SEL_BG   = 0x0319;
static const uint32_t C_DIM      = 0x8410;
static const uint32_t C_HINT     = 0x528A;

static const int W = 240;
static const int H = 135;

// ── Hilfsfunktionen ───────────────────────────────────────────────────────────
static decltype(M5Cardputer.Display)& D() { return M5Cardputer.Display; }

static void clear() { D().fillScreen(C_BG); }

static void drawHeader(const char* title, uint32_t color) {
    D().fillRect(0, 0, W, 22, color);
    D().setTextColor(TFT_WHITE, color);
    D().setTextSize(1);
    D().setCursor(6, 7);
    D().print(title);
    D().setTextColor(TFT_WHITE, C_BG);
}

static void drawFooter(const char* hint) {
    D().fillRect(0, H - 16, W, 16, 0x2104);
    D().setTextColor(C_HINT, 0x2104);
    D().setTextSize(1);
    D().setCursor(4, H - 11);
    D().print(hint);
    D().setTextColor(TFT_WHITE, C_BG);
}

// Balken von x,y mit Breite barW, Füllstand 0–100, Farbe
static void drawBar(int x, int y, int barW, int h,
                    int pct, uint32_t fillColor) {
    D().drawRect(x, y, barW, h, C_DIM);
    int fill = barW * pct / 100;
    if (fill > 0) D().fillRect(x + 1, y + 1, fill - 1, h - 2, fillColor);
    if (fill < barW - 1)
        D().fillRect(x + 1 + fill, y + 1, barW - 2 - fill, h - 2, C_BG);
}

// Batterie-Indikator oben rechts im Header (12×8 px)
static void drawBattery(int pct, bool charging) {
    int bx = W - 30;
    int by = 7;
    // Umriss
    D().drawRect(bx, by, 20, 8, TFT_WHITE);
    D().fillRect(bx + 20, by + 2, 2, 4, TFT_WHITE);  // Pol

    uint32_t col = (pct > 50) ? TFT_GREEN
                 : (pct > 20) ? TFT_YELLOW
                              : TFT_RED;
    int fill = 18 * pct / 100;
    D().fillRect(bx + 1, by + 1, fill, 6, col);
    D().fillRect(bx + 1 + fill, by + 1, 18 - fill, 6, C_BG);

    // Ladepfeil wenn charging
    if (charging) {
        D().setTextColor(TFT_WHITE, col);
        D().setTextSize(1);
        D().setCursor(bx + 5, by);
        D().print("+");
    }

    // Prozentzahl neben Batterie
    D().setTextColor(TFT_WHITE, C_HDR_IDLE);
    D().setTextSize(1);
    D().setCursor(W - 8 - (pct < 10 ? 6 : pct < 100 ? 12 : 18), by);
    if (pct >= 0) D().printf("%d%%", pct);
}

static void drawInputField(int y, const char* label,
                            const String& value, bool active) {
    uint32_t border = active ? TFT_CYAN : C_DIM;
    D().drawRect(6, y, W - 12, 22, border);
    D().setTextColor(active ? TFT_CYAN : C_DIM, C_BG);
    D().setTextSize(1);
    D().setCursor(10, y + 3);
    D().print(label);
    D().setTextColor(TFT_WHITE, C_BG);
    int valX = 10 + strlen(label) * 6 + 4;
    D().fillRect(valX, y + 1, W - 12 - valX + 6, 20, C_BG);
    D().setCursor(valX, y + 3);
    D().print(value + (active ? "_" : ""));
}

// ── Icon-Zeichner ─────────────────────────────────────────────────────────────
// idx 0–4 = Aufnahme / Dateien / WLAN / Server / SD-Karte
// cx,cy = Mittelpunkt, sz = Größe des Icon-Quadrats, col = Vordergrund, bg = Hintergrund
static void drawIcon(int idx, int cx, int cy, int sz, uint32_t col, uint32_t bg) {
    switch (idx % 5) {

    case 0: {  // Mikrofon – Aufnahme starten
        int bw = max(3, sz * 3 / 14);
        int bh = max(5, sz * 5 / 14);
        int top = cy - bh - sz / 8;
        // Kapsel
        D().fillRoundRect(cx - bw, top, bw * 2, bh * 2, bw, col);
        // Membran-Schlitze
        if (sz >= 36) {
            D().drawFastHLine(cx - bw + 2, top + bh / 2, bw * 2 - 4, bg);
            D().drawFastHLine(cx - bw + 2, top + bh,     bw * 2 - 4, bg);
            D().drawFastHLine(cx - bw + 2, top + bh * 3 / 2, bw * 2 - 4, bg);
        }
        // Ständer
        D().fillRect(cx - 1, cy + bh - sz / 8, 3, sz / 5 + 1, col);
        // Fuß
        D().fillRect(cx - bw - 1, cy + bh - sz / 8 + sz / 5, bw * 2 + 2, 3, col);
        break;
    }

    case 1: {  // Ordner – Aufnahmen ansehen
        int fw  = sz * 6 / 14;
        int fh  = sz * 5 / 14;
        int tabW = fw * 3 / 4;
        int tabH = max(3, sz / 9);
        // Körper
        D().fillRoundRect(cx - fw, cy - fh + tabH, fw * 2, fh * 2 - tabH, 3, col);
        // Tab oben-links (überschreibt Header-Ecke des Körpers und zeichnet Tab)
        D().fillRect(cx - fw - 1, cy - fh, fw + 1, tabH + 2, bg);
        D().fillRoundRect(cx - fw, cy - fh, tabW, tabH + 2, 2, col);
        // Dateilinien im Körper
        if (sz >= 34) {
            int lw = fw * 2 - 8;
            D().drawFastHLine(cx - fw + 4, cy - fh + tabH + fh * 2 / 3,     lw, bg);
            D().drawFastHLine(cx - fw + 4, cy - fh + tabH + fh * 4 / 3 + 1, lw, bg);
        }
        break;
    }

    case 2: {  // WLAN – WLAN einrichten
        int yOff = sz * 3 / 10;
        int dotR = max(2, sz / 12);
        D().fillCircle(cx, cy + yOff, dotR, col);
        // Drei konzentrische Kreise
        int rads[3] = { sz / 6, sz * 2 / 7, sz * 3 / 7 };
        for (int i = 0; i < 3; i++)
            D().drawCircle(cx, cy + yOff, rads[i], col);
        // Untere Hälfte ab Mittelpunkt abdecken (Kreisbögen → Bögen)
        D().fillRect(cx - sz / 2 - 1, cy + yOff + dotR,
                     sz + 2, sz / 2 - yOff + sz / 8 + 2, bg);
        break;
    }

    case 3: {  // Server – Server prüfen
        int sw     = sz * 11 / 16;
        int rh     = max(4, sz / 5);
        int gap    = max(2, sz / 16);
        int totalH = 3 * rh + 2 * gap;
        int y0     = cy - totalH / 2;
        for (int i = 0; i < 3; i++) {
            int ry = y0 + i * (rh + gap);
            D().drawRoundRect(cx - sw / 2, ry, sw, rh, 2, col);
            // Status-LED
            D().fillCircle(cx + sw / 2 - 4, ry + rh / 2, 2, col);
            // Disk-Slot-Linie
            if (sz >= 34)
                D().drawFastHLine(cx - sw / 2 + 3, ry + rh / 2, sw / 2, col);
        }
        break;
    }

    case 4: {  // SD-Karte – SD-Karte prüfen
        int cw    = sz * 9 / 16;
        int ch    = sz * 11 / 16;
        int notch = max(3, sz / 6);
        int x0    = cx - cw / 2;
        int y0    = cy - ch / 2;
        // Hauptkörper
        D().fillRect(x0, y0, cw, ch, col);
        // Rechte obere Ecke (Notch) abschneiden
        D().fillTriangle(x0 + cw - notch, y0,
                         x0 + cw, y0,
                         x0 + cw, y0 + notch, bg);
        // Schräge Kante
        D().drawLine(x0 + cw - notch, y0, x0 + cw, y0 + notch, C_DIM);
        // Kontaktstreifen (helle Streifen = Kontakte, dunkle Lücken)
        if (sz >= 34) {
            int nC  = 5;
            int cGap = 1;
            int cWid = (cw - 8 - (nC - 1) * cGap) / nC;
            for (int i = 0; i < nC; i++)
                D().fillRect(x0 + 4 + i * (cWid + cGap),
                             y0 + ch * 3 / 5, cWid, ch / 5, bg);
        }
        break;
    }
    }
}

// ── Öffentliche Funktionen ────────────────────────────────────────────────────

void Display::init() {
    D().setRotation(1);
    D().setTextSize(1);
    D().setTextColor(TFT_WHITE, C_BG);
    clear();
}

void Display::showMenu(const char* const items[], int count, int selected,
                       int battPct, bool charging) {
    clear();

    // ── Header ────────────────────────────────────────────────────────────────
    D().fillRect(0, 0, W, 22, C_HDR_IDLE);
    D().setTextColor(TFT_WHITE, C_HDR_IDLE);
    D().setTextSize(1);
    D().setCursor(6, 7);
    D().print("Diktiergeraet");
    if (battPct >= 0) drawBattery(battPct, charging);
    D().setTextColor(TFT_WHITE, C_BG);

    // ── Auswahl-Hintergrund (Mitte) ───────────────────────────────────────────
    D().fillRoundRect(82, 24, 76, 68, 6, C_SEL_BG);
    D().fillRect(82, 26, 4, 64, TFT_CYAN);

    // ── Nachbar-Indizes (wrapping) ────────────────────────────────────────────
    int idxL = (selected - 1 + count) % count;
    int idxR = (selected + 1) % count;

    // ── Icons ─────────────────────────────────────────────────────────────────
    // Seitenicons: kleiner, gedimmt
    drawIcon(idxL,    40, 56, 32, C_DIM,     C_BG);
    drawIcon(selected, 120, 56, 44, TFT_WHITE, C_SEL_BG);
    drawIcon(idxR,   200, 56, 32, C_DIM,     C_BG);

    // ── Navigationspfeile ─────────────────────────────────────────────────────
    D().setTextSize(2);
    D().setTextColor(C_DIM, C_BG);
    D().setCursor(1, 48);
    D().print("<");
    D().setCursor(225, 48);
    D().print(">");
    D().setTextSize(1);

    // ── Item-Nummer der Seitenicons ───────────────────────────────────────────
    D().setTextColor(C_DIM, C_BG);
    D().setCursor(35, 76);
    D().printf("%d", idxL + 1);
    D().setCursor(195, 76);
    D().printf("%d", idxR + 1);

    // ── Label des gewählten Items ─────────────────────────────────────────────
    D().setTextColor(TFT_WHITE, C_BG);
    String lbl = items[selected];
    D().setCursor(max(0, (W - (int)lbl.length() * 6) / 2), 97);
    D().print(lbl);

    // ── Seitenindikator-Punkte ────────────────────────────────────────────────
    const int dotStep = 10;
    int dotX0 = W / 2 - (count - 1) * dotStep / 2;
    for (int i = 0; i < count; i++)
        D().fillCircle(dotX0 + i * dotStep, 109, 2,
                       i == selected ? TFT_WHITE : C_DIM);

    drawFooter("A/D Nav   ENTER OK   1-5 Direkt");
    D().setTextColor(TFT_WHITE, C_BG);
}

void Display::showRecording(uint32_t seconds, int gain, int level) {
    clear();
    drawHeader("  RECORDING", C_HDR_REC);

    // Große Zeitanzeige
    char buf[8];
    snprintf(buf, sizeof(buf), "%02lu:%02lu", seconds / 60, seconds % 60);
    D().setTextSize(3);
    int tw = strlen(buf) * 18;
    D().setCursor((W - tw) / 2, 38);
    D().setTextColor(TFT_WHITE, C_BG);
    D().print(buf);
    D().setTextSize(1);

    // VU-Meter
    D().setTextColor(C_DIM, C_BG);
    D().setCursor(6, 82);
    D().print("Pegel:");
    uint32_t vuColor = (level < 70) ? TFT_GREEN
                     : (level < 90) ? TFT_YELLOW
                                    : TFT_RED;
    drawBar(50, 80, W - 56, 10, level, vuColor);

    // Gain-Anzeige
    D().setTextColor(C_DIM, C_BG);
    D().setCursor(6, 96);
    D().printf("Gain: %dx", gain);
    D().setTextColor(C_HINT, C_BG);
    D().setCursor(60, 96);
    D().print("(+/- anpassen)");

    drawFooter("ENTER = Aufnahme stoppen");
    D().setTextColor(TFT_WHITE, C_BG);
}

void Display::updateRecording(uint32_t seconds, int gain, int level) {
    // Blinkendes Aufnahmesymbol
    bool blink = (seconds % 2 == 0);
    D().fillRect(0, 0, 22, 22, C_HDR_REC);
    D().setTextColor(blink ? TFT_WHITE : C_HDR_REC, C_HDR_REC);
    D().setTextSize(1);
    D().setCursor(6, 7);
    D().print("*");
    D().setTextColor(TFT_WHITE, C_HDR_REC);
    D().setCursor(18, 7);
    D().print(" RECORDING");

    // Zeit aktualisieren
    char buf[8];
    snprintf(buf, sizeof(buf), "%02lu:%02lu", seconds / 60, seconds % 60);
    D().setTextSize(3);
    D().setTextColor(TFT_WHITE, C_BG);
    int tw = strlen(buf) * 18;
    D().fillRect(0, 34, W, 34, C_BG);
    D().setCursor((W - tw) / 2, 38);
    D().print(buf);
    D().setTextSize(1);

    // VU-Meter (nur Bar neu zeichnen)
    uint32_t vuColor = (level < 70) ? TFT_GREEN
                     : (level < 90) ? TFT_YELLOW
                                    : TFT_RED;
    drawBar(50, 80, W - 56, 10, level, vuColor);

    // Gain (nur wenn nötig; hier immer aktuell)
    D().fillRect(6, 94, 100, 10, C_BG);
    D().setTextColor(C_DIM, C_BG);
    D().setCursor(6, 96);
    D().printf("Gain: %dx", gain);
}

void Display::showConfirmUpload(const String& filename) {
    clear();
    drawHeader("Aufnahme beendet", C_HDR_WARN);
    D().setTextSize(1);
    D().setTextColor(TFT_WHITE, C_BG);
    D().setCursor(6, 30);
    D().print("Datei uebermitteln?");
    D().setTextColor(C_DIM, C_BG);
    D().setCursor(6, 46);
    int slash = filename.lastIndexOf('/');
    D().print(filename.substring(slash + 1));

    D().fillRect(10,  73, 90, 28, 0x2724);
    D().fillRect(140, 73, 90, 28, 0x6000);
    D().setTextColor(TFT_WHITE, 0x2724);
    D().setCursor(36, 83);
    D().print("[J] Ja");
    D().setTextColor(TFT_WHITE, 0x6000);
    D().setCursor(159, 83);
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
    D().drawRect(10, 60, W - 20, 12, C_DIM);
    D().setTextColor(C_DIM, C_BG);
    D().setCursor(6, 80);
    D().print("Bitte warten.");
}

void Display::showUploadProgress(uint32_t sentKB, uint32_t totalKB) {
    // Nur Fortschrittsbalken + Text aktualisieren (kein clear)
    int pct = (totalKB > 0) ? (int)((uint64_t)sentKB * 100 / totalKB) : 0;
    if (pct > 100) pct = 100;
    drawBar(10, 60, W - 20, 12, pct, TFT_CYAN);

    D().fillRect(6, 77, W - 12, 12, C_BG);
    D().setTextColor(TFT_CYAN, C_BG);
    D().setTextSize(1);
    D().setCursor(6, 80);
    D().printf("%u / %u KB   %d%%", sentKB, totalKB, pct);
}

void Display::showUploadOk(const String& jobId) {
    clear();
    drawHeader("Hochgeladen!", C_HDR_OK);
    D().setTextSize(1);
    D().setTextColor(TFT_GREEN, C_BG);
    D().setCursor(6, 33);
    D().print("Datei uebermittelt");
    D().setTextColor(C_DIM, C_BG);
    D().setCursor(6, 49);
    D().print("Job-ID:");
    D().setTextColor(TFT_WHITE, C_BG);
    D().setCursor(52, 49);
    D().print(jobId.length() > 0 ? jobId : "---");
    D().setTextColor(TFT_CYAN, C_BG);
    D().setCursor(6, 65);
    D().print("Pruefe Server-Status...");
    drawFooter("ENTER = Zum Menu (laeuft weiter)");
    D().setTextColor(TFT_WHITE, C_BG);
}

void Display::showUploadFail(const String& reason) {
    clear();
    drawHeader("Fehler!", C_HDR_REC);
    D().setTextSize(1);
    D().setTextColor(TFT_RED, C_BG);
    D().setCursor(6, 33);
    D().print("Uebertragung fehlgeschl.");
    D().setTextColor(C_DIM, C_BG);
    D().setCursor(6, 49);
    D().print(reason.substring(0, 36));
    D().setTextColor(TFT_YELLOW, C_BG);
    D().setCursor(6, 65);
    D().print("Datei in Queue gespeichert.");
    drawFooter("ENTER = Zurueck");
    D().setTextColor(TFT_WHITE, C_BG);
}

void Display::showJobPoll(const String& jobId, const String& status,
                          uint32_t elapsedSec) {
    // Nur den Status-Bereich neu zeichnen (Header bleibt)
    D().fillRect(0, 23, W, H - 39, C_BG);

    D().setTextSize(1);
    D().setTextColor(C_DIM, C_BG);
    D().setCursor(6, 27);
    D().print("Job: ");
    D().setTextColor(TFT_WHITE, C_BG);
    D().print(jobId);

    // Status mit Icon
    String icon;
    uint32_t col;
    if      (status == "queued")       { icon = "[ ]"; col = C_DIM;      }
    else if (status == "transcribing") { icon = "[T]"; col = TFT_YELLOW; }
    else if (status == "processing")   { icon = "[~]"; col = TFT_CYAN;   }
    else if (status == "summarizing")  { icon = "[S]"; col = 0xFD20;     } // Orange
    else if (status == "done")         { icon = "[OK]";col = TFT_GREEN;  }
    else                               { icon = "[!!]";col = TFT_RED;    }

    D().setTextSize(1);
    D().setTextColor(col, C_BG);
    D().setCursor(6, 45);
    D().printf("%s %s", icon.c_str(), status.c_str());

    // Wartezeit
    D().setTextColor(C_DIM, C_BG);
    D().setCursor(6, 63);
    D().printf("Warte: %lus", elapsedSec);

    // Drehende Animation
    const char spinner[] = "|/-\\";
    D().setCursor(W - 16, 45);
    D().setTextColor(TFT_CYAN, C_BG);
    D().print(spinner[(elapsedSec) % 4]);

    drawFooter("ENTER = Zum Menue");
    D().setTextColor(TFT_WHITE, C_BG);
}

void Display::showFileList(const std::vector<RecFileEntry>& files,
                           int selected, int offset) {
    clear();

    char hdr[32];
    snprintf(hdr, sizeof(hdr), "Aufnahmen (%d)", (int)files.size());
    drawHeader(hdr, C_HDR_IDLE);

    if (files.empty()) {
        D().setTextColor(C_DIM, C_BG);
        D().setCursor(6, 55);
        D().print("Keine Aufnahmen auf SD.");
        drawFooter("ESC = Zurueck");
        D().setTextColor(TFT_WHITE, C_BG);
        return;
    }

    const int ITEM_H  = 22;
    const int START_Y = 23;
    const int VISIBLE = (H - START_Y - 16) / ITEM_H;  // sichtbare Zeilen

    for (int i = 0; i < VISIBLE; i++) {
        int idx = offset + i;
        if (idx >= (int)files.size()) break;

        int  y   = START_Y + i * ITEM_H;
        bool sel = (idx == selected);

        D().fillRect(0, y, W, ITEM_H, sel ? C_SEL_BG : C_BG);
        if (sel) D().fillRect(0, y, 4, ITEM_H, TFT_CYAN);

        D().setTextColor(sel ? TFT_WHITE : C_DIM, sel ? C_SEL_BG : C_BG);
        D().setTextSize(1);
        D().setCursor(10, y + 4);
        // Dateiname (max. 22 Zeichen) + Größe rechtsbündig
        String name = files[idx].name;
        if (name.length() > 22) name = name.substring(0, 20) + "..";
        D().print(name);

        // Größe rechtsbündig
        char sz[10];
        if (files[idx].sizeKB >= 1024)
            snprintf(sz, sizeof(sz), "%d.%dM",
                     files[idx].sizeKB / 1024,
                     (files[idx].sizeKB % 1024) * 10 / 1024);
        else
            snprintf(sz, sizeof(sz), "%dK", files[idx].sizeKB);
        D().setCursor(W - strlen(sz) * 6 - 6, y + 4);
        D().print(sz);
    }

    // Scroll-Indikator
    if ((int)files.size() > VISIBLE) {
        D().setTextColor(C_DIM, C_BG);
        D().setCursor(W - 10, START_Y);
        D().print(offset > 0 ? "^" : " ");
        D().setCursor(W - 10, H - 20);
        D().print(offset + VISIBLE < (int)files.size() ? "v" : " ");
    }

    drawFooter("W/S Nav   ENTER Senden   ESC Zurueck");
    D().setTextColor(TFT_WHITE, C_BG);
}

void Display::showWifiScan(const std::vector<WifiNetwork>& nets,
                            int selected, int offset, bool scanning) {
    clear();
    drawHeader("WLAN auswaehlen", C_HDR_IDLE);

    if (scanning) {
        D().setTextColor(TFT_CYAN, C_BG);
        D().setTextSize(1);
        D().setCursor(20, 52);
        D().print("Suche Netzwerke...");
        drawFooter("Bitte warten");
        D().setTextColor(TFT_WHITE, C_BG);
        return;
    }

    if (nets.empty()) {
        D().setTextColor(C_DIM, C_BG);
        D().setTextSize(1);
        D().setCursor(20, 52);
        D().print("Keine Netzwerke gefunden.");
        drawFooter("R=Erneut   DEL=Zurueck");
        D().setTextColor(TFT_WHITE, C_BG);
        return;
    }

    const int ITEM_H  = 22;
    const int START_Y = 23;
    const int VISIBLE = (H - START_Y - 16) / ITEM_H;

    for (int i = 0; i < VISIBLE; i++) {
        int idx = offset + i;
        if (idx >= (int)nets.size()) break;

        int  y   = START_Y + i * ITEM_H;
        bool sel = (idx == selected);

        D().fillRect(0, y, W, ITEM_H, sel ? C_SEL_BG : C_BG);
        if (sel) D().fillRect(0, y, 4, ITEM_H, TFT_CYAN);

        // SSID-Text (max. 24 Zeichen)
        String name = nets[idx].ssid.length() > 0 ? nets[idx].ssid : "(versteckt)";
        if (name.length() > 24) name = name.substring(0, 22) + "..";
        D().setTextColor(sel ? TFT_WHITE : C_DIM, sel ? C_SEL_BG : C_BG);
        D().setTextSize(1);
        D().setCursor(10, y + 4);
        D().print(name);

        // Schloss-Symbol wenn verschlüsselt
        if (nets[idx].encrypted) {
            D().setTextColor(sel ? TFT_YELLOW : 0xC5E0, sel ? C_SEL_BG : C_BG);
            D().setCursor(W - 34, y + 4);
            D().print("[+]");
        }

        // Signalbalken (4 Stufen, aufsteigend) rechts unten im Item
        int rssi  = nets[idx].rssi;
        int bars  = (rssi >= -60) ? 4 : (rssi >= -70) ? 3 : (rssi >= -80) ? 2 : 1;
        int bx    = W - 18;
        int baseY = y + ITEM_H - 3;
        for (int b = 0; b < 4; b++) {
            int bh  = 2 + b * 2;                     // Höhen: 2,4,6,8px
            uint32_t col = (b < bars)
                ? (sel ? TFT_WHITE : TFT_GREEN)
                : (sel ? 0x2945 : C_DIM);
            D().fillRect(bx + b * 4, baseY - bh, 3, bh, col);
        }
    }

    // Scroll-Indikatoren
    if ((int)nets.size() > VISIBLE) {
        D().setTextColor(C_DIM, C_BG);
        D().setCursor(W - 10, START_Y);
        D().print(offset > 0 ? "^" : " ");
        D().setCursor(W - 10, H - 20);
        D().print(offset + VISIBLE < (int)nets.size() ? "v" : " ");
    }

    drawFooter("W/S:Nav  ENTER:Ausw.  R:Scan  DEL:Zur.");
    D().setTextColor(TFT_WHITE, C_BG);
}

void Display::showWifiPass(const String& ssid, const String& pass) {
    clear();
    drawHeader("Passwort eingeben", C_HDR_IDLE);

    D().setTextSize(1);
    D().setTextColor(C_DIM, C_BG);
    D().setCursor(6, 28);
    D().print("Netzwerk:");
    D().setTextColor(TFT_CYAN, C_BG);
    D().setCursor(64, 28);
    String s = ssid;
    if (s.length() > 22) s = s.substring(0, 20) + "..";
    D().print(s);

    // Passwort im Klartext
    drawInputField(50, "Pass: ", pass, true);

    // Dynamischer Footer: leer → zurück möglich
    if (pass.length() == 0)
        drawFooter("DEL=Zurueck  ENTER=Verbinden");
    else
        drawFooter("DEL=loeschen  ENTER=Verbinden");

    D().setTextColor(TFT_WHITE, C_BG);
}

void Display::showServerCheck(const String& host, int port) {
    clear();
    drawHeader("Server pruefen", C_HDR_IDLE);
    D().setTextSize(1);
    D().setTextColor(TFT_WHITE, C_BG);
    D().setCursor(6, 30);
    D().printf("Host: %s:%d", host.c_str(), port);
    D().setTextColor(TFT_CYAN, C_BG);
    D().setCursor(6, 50);
    D().print("Verbinde...");
}

void Display::showServerResult(bool ok, const String& detail) {
    D().fillRect(0, 48, W, H - 64, C_BG);
    D().setTextSize(1);
    D().setTextColor(ok ? TFT_GREEN : TFT_RED, C_BG);
    D().setCursor(6, 50);
    D().print(ok ? "Server erreichbar  OK" : "Nicht erreichbar!");
    D().setTextColor(C_DIM, C_BG);
    D().setCursor(6, 66);
    D().print(detail.substring(0, 38));
    drawFooter("ENTER = Zurueck");
    D().setTextColor(TFT_WHITE, C_BG);
}

void Display::showSdResult(bool ok, const String& cardType,
                           uint64_t totalMB, uint64_t usedMB, int recFiles) {
    clear();
    drawHeader("SD-Karte", ok ? C_HDR_OK : C_HDR_REC);
    D().setTextSize(1);

    if (!ok) {
        D().setTextColor(TFT_RED, C_BG);
        D().setCursor(6, 35);
        D().print("Keine SD-Karte gefunden!");
    } else {
        uint64_t freeMB  = totalMB - usedMB;
        uint32_t freePct = (totalMB > 0) ? (uint32_t)(freeMB * 100 / totalMB) : 0;

        D().setTextColor(C_DIM, C_BG); D().setCursor(6, 27); D().print("Typ:");
        D().setTextColor(TFT_WHITE, C_BG); D().setCursor(40, 27); D().print(cardType);

        D().setTextColor(C_DIM, C_BG); D().setCursor(6, 43); D().print("Gesamt:");
        D().setTextColor(TFT_WHITE, C_BG); D().setCursor(52, 43);
        D().printf("%llu MB", totalMB);

        uint32_t freeColor = (freePct > 20) ? TFT_GREEN
                           : (freePct >  5) ? TFT_YELLOW
                                            : TFT_RED;
        D().setTextColor(C_DIM, C_BG); D().setCursor(6, 59); D().print("Frei:");
        D().setTextColor(freeColor, C_BG); D().setCursor(40, 59);
        D().printf("%llu MB (%lu%%)", freeMB, freePct);

        D().setTextColor(C_DIM, C_BG); D().setCursor(6, 75); D().print("Aufnahmen:");
        D().setTextColor(TFT_WHITE, C_BG); D().setCursor(70, 75);
        D().printf("%d in /rec", recFiles);

        drawBar(10, 93, W - 20, 10, 100 - freePct, freeColor);
    }

    drawFooter("ENTER = Zurueck");
    D().setTextColor(TFT_WHITE, C_BG);
}

void Display::showCaptivePortal(const String& apName, const String& ip) {
    clear();
    drawHeader("WLAN Setup", C_HDR_IDLE);

    D().setTextSize(1);

    D().setTextColor(C_DIM, C_BG);
    D().setCursor(6, 28);
    D().print("AP-Name:");
    D().setTextColor(TFT_CYAN, C_BG);
    D().setCursor(58, 28);
    D().print(apName);

    D().setTextColor(C_DIM, C_BG);
    D().setCursor(6, 44);
    D().print("IP:");
    D().setTextColor(TFT_WHITE, C_BG);
    D().setCursor(28, 44);
    D().print(ip);

    D().setTextColor(TFT_YELLOW, C_BG);
    D().setCursor(6, 62);
    D().print("1. Mit AP verbinden");
    D().setCursor(6, 76);
    D().print("2. Browser: http://");
    D().setTextColor(TFT_WHITE, C_BG);
    D().setCursor(6, 90);
    D().print(ip);

    D().setTextColor(C_DIM, C_BG);
    D().setCursor(6, 106);
    D().print("Warte auf Konfiguration...");

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
