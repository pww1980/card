#pragma once
#include <Arduino.h>
#include <vector>

// Eintrag für den File-Browser
struct RecFileEntry {
    String   name;    // Dateiname ohne Pfad
    uint32_t sizeKB;
};

namespace Display {
    void init();

    // ── Hauptmenü ─────────────────────────────────────────────────────────────
    // battPct: 0–100 (-1 = unbekannt), charging: USB angeschlossen
    void showMenu(const char* const items[], int count, int selected,
                  int battPct = -1, bool charging = false);

    // ── Aufnahme ──────────────────────────────────────────────────────────────
    void showRecording(uint32_t seconds, int gain, int level);
    // Nur Zeit + VU-Meter aktualisieren (kein full redraw)
    void updateRecording(uint32_t seconds, int gain, int level);

    // ── Bestätigungsdialog ────────────────────────────────────────────────────
    void showConfirmUpload(const String& filename);

    // ── Upload-Status ─────────────────────────────────────────────────────────
    void showUploading();
    void showUploadOk(const String& jobId);
    void showUploadFail(const String& reason);

    // ── Job-Status-Polling ────────────────────────────────────────────────────
    void showJobPoll(const String& jobId, const String& status,
                     uint32_t elapsedSec);

    // ── File-Browser ─────────────────────────────────────────────────────────
    // files: sortierte Liste, selected: absoluter Index, offset: Scroll-Offset
    void showFileList(const std::vector<RecFileEntry>& files,
                      int selected, int offset);

    // ── WLAN-Setup ────────────────────────────────────────────────────────────
    void showWifiSetup(const String& ssid, const String& pass, int field);

    // ── Server-Check ──────────────────────────────────────────────────────────
    void showServerCheck(const String& host, int port);
    void showServerResult(bool ok, const String& detail);

    // ── SD-Karten-Check ───────────────────────────────────────────────────────
    void showSdResult(bool ok, const String& cardType,
                      uint64_t totalMB, uint64_t usedMB, int recFiles);

    // ── Allgemein ─────────────────────────────────────────────────────────────
    void showMessage(const String& msg);
    void showError(const String& msg);
}
