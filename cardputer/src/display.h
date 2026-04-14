#pragma once
#include <Arduino.h>

namespace Display {
    void init();

    // ── Hauptmenü ─────────────────────────────────────────────────────────────
    // items: Array mit Menütexten, count: Anzahl, selected: aktiver Index
    void showMenu(const char* const items[], int count, int selected);

    // ── Aufnahme ──────────────────────────────────────────────────────────────
    void showRecording(uint32_t seconds);   // Vollständige Recording-Anzeige
    void updateRecordingTime(uint32_t seconds); // Nur die Zeit aktualisieren

    // ── Bestätigungsdialog ────────────────────────────────────────────────────
    void showConfirmUpload(const String& filename);

    // ── Upload-Status ─────────────────────────────────────────────────────────
    void showUploading();
    void showUploadOk(const String& jobId);
    void showUploadFail(const String& reason);

    // ── WLAN-Setup ────────────────────────────────────────────────────────────
    // field: 0=SSID aktiv, 1=Passwort aktiv
    void showWifiSetup(const String& ssid, const String& pass, int field);

    // ── Server-Check ──────────────────────────────────────────────────────────
    void showServerCheck(const String& host, int port);
    void showServerResult(bool ok, const String& detail);

    // ── SD-Karten-Check ───────────────────────────────────────────────────────
    // totalMB/usedMB: Kapazität in MB, recFiles: Anzahl Dateien in /rec
    void showSdResult(bool ok, const String& cardType,
                      uint64_t totalMB, uint64_t usedMB, int recFiles);

    // ── Allgemein ─────────────────────────────────────────────────────────────
    void showMessage(const String& msg);       // Temporäre Info-Meldung
    void showError(const String& msg);         // Fehler-Anzeige
}
