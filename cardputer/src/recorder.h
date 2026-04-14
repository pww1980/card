#pragma once
#include <Arduino.h>

namespace Recorder {
    // Aufnahme starten/stoppen
    bool start(const String& filePath);
    void stop();

    // Im Loop aufrufen: Puffer lesen, Gain anwenden, auf SD schreiben
    void tick();

    // Status
    bool     isRunning();
    uint32_t elapsedSeconds();

    // Software-Gain: 1–16 (Faktor auf die Sample-Werte angewendet)
    // Standardwert: 4. Änderung wirkt sofort, auch während der Aufnahme.
    void setGain(int gain);
    int  getGain();

    // Aktueller Eingangspegel 0–100 (Peak aus letztem Buffer, normalisiert)
    int  getLevel();
}
