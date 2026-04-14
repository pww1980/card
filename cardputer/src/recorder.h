#pragma once
#include <Arduino.h>

namespace Recorder {
    // Aufnahme starten, WAV-Datei auf SD anlegen
    // Gibt false zurück wenn SD-Fehler
    bool start(const String& filePath);

    // Aufnahme stoppen und WAV-Header finalisieren
    void stop();

    // Im Loop aufrufen: I2S-Puffer lesen und in Datei schreiben
    void tick();

    // Vergangene Sekunden seit Start
    uint32_t elapsedSeconds();

    bool isRunning();
}
