#pragma once
#include <Arduino.h>

namespace Uploader {
    // Laufzeit-Serverkonfiguration setzen (Fallback: config.h)
    void setServer(const String& host, int port);

    // Datei per HTTP multipart/form-data hochladen
    // jobId wird bei Erfolg befüllt
    bool upload(const String& filePath, String& jobId);

    // Datei zur Offline-Queue hinzufügen (SD: QUEUE_FILE)
    void addToQueue(const String& filePath);

    // Gespeicherte Queue abarbeiten (aufrufen wenn WiFi verfügbar)
    void processQueue();
}
