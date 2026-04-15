#pragma once
#include <Arduino.h>

// Upload-Fortschritts-Callback: sentBytes, totalBytes
typedef void (*ProgressCb)(uint32_t, uint32_t);

namespace Uploader {
    void setServer(const String& host, int port);

    // Fortschritts-Callback setzen (nullptr = deaktivieren)
    void setProgressCb(ProgressCb cb);

    bool upload(const String& filePath, String& jobId);
    void addToQueue(const String& filePath);
    void processQueue();
}
