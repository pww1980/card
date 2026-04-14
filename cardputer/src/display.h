#pragma once
#include <Arduino.h>

namespace Display {
    void init();
    void showIdle();
    void showMessage(const String& msg);
    void showError(const String& msg);
    void showRecording();
    void updateRecordingTime(uint32_t seconds);
    void showUploading();
    void showUploadOk(const String& jobId);
}
