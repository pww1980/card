#include "display.h"
#include <M5Cardputer.h>

static M5Canvas canvas(&M5Cardputer.Display);

static void clear() {
    M5Cardputer.Display.fillScreen(TFT_BLACK);
}

static void header(const char* title, uint32_t color) {
    M5Cardputer.Display.fillRect(0, 0, 240, 24, color);
    M5Cardputer.Display.setTextColor(TFT_WHITE, color);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setCursor(6, 6);
    M5Cardputer.Display.print(title);
    M5Cardputer.Display.setTextColor(TFT_WHITE, TFT_BLACK);
}

void Display::init() {
    M5Cardputer.Display.setRotation(1);
    M5Cardputer.Display.setTextSize(1);
    clear();
}

void Display::showIdle() {
    clear();
    header("Diktiergeraet", TFT_DARKGREY);
    M5Cardputer.Display.setCursor(6, 32);
    M5Cardputer.Display.print("[R] Aufnahme starten");
    M5Cardputer.Display.setCursor(6, 48);
    M5Cardputer.Display.print("[U] Letzten Upload wiederholen");
}

void Display::showMessage(const String& msg) {
    clear();
    M5Cardputer.Display.setCursor(6, 40);
    M5Cardputer.Display.setTextColor(TFT_CYAN, TFT_BLACK);
    M5Cardputer.Display.print(msg);
    M5Cardputer.Display.setTextColor(TFT_WHITE, TFT_BLACK);
}

void Display::showError(const String& msg) {
    clear();
    header("FEHLER", TFT_RED);
    M5Cardputer.Display.setCursor(6, 32);
    M5Cardputer.Display.setTextColor(TFT_RED, TFT_BLACK);
    M5Cardputer.Display.print(msg);
    M5Cardputer.Display.setTextColor(TFT_WHITE, TFT_BLACK);
}

void Display::showRecording() {
    clear();
    header("● REC", TFT_RED);
    M5Cardputer.Display.setCursor(6, 32);
    M5Cardputer.Display.print("00:00");
    M5Cardputer.Display.setCursor(6, 52);
    M5Cardputer.Display.setTextColor(TFT_DARKGREY, TFT_BLACK);
    M5Cardputer.Display.print("[R] Stoppen");
    M5Cardputer.Display.setTextColor(TFT_WHITE, TFT_BLACK);
}

void Display::updateRecordingTime(uint32_t seconds) {
    char buf[8];
    snprintf(buf, sizeof(buf), "%02lu:%02lu", seconds / 60, seconds % 60);
    M5Cardputer.Display.setCursor(6, 32);
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.print(buf);
    M5Cardputer.Display.setTextSize(1);
}

void Display::showUploading() {
    clear();
    header("UPLOAD", TFT_BLUE);
    M5Cardputer.Display.setCursor(6, 32);
    M5Cardputer.Display.print("Sende an Server...");
}

void Display::showUploadOk(const String& jobId) {
    clear();
    header("OK", TFT_GREEN);
    M5Cardputer.Display.setCursor(6, 32);
    M5Cardputer.Display.setTextColor(TFT_GREEN, TFT_BLACK);
    M5Cardputer.Display.print("Hochgeladen!");
    M5Cardputer.Display.setCursor(6, 48);
    M5Cardputer.Display.setTextColor(TFT_DARKGREY, TFT_BLACK);
    M5Cardputer.Display.print("Job: " + jobId.substring(0, 8));
    M5Cardputer.Display.setTextColor(TFT_WHITE, TFT_BLACK);
}
