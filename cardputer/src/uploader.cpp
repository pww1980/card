#include "uploader.h"
#include "config.h"

#include <WiFi.h>
#include <HTTPClient.h>
#include <SD.h>
#include <ArduinoJson.h>

static const String SERVER_URL =
    "http://" + String(SERVER_HOST) + ":" + String(SERVER_PORT) + UPLOAD_PATH;

// ── Hilfsfunktion: multipart/form-data Upload ─────────────────────────────────
bool Uploader::upload(const String& filePath, String& jobId) {
    File f = SD.open(filePath, FILE_READ);
    if (!f) return false;

    HTTPClient http;
    http.begin(SERVER_URL);
    http.setTimeout(UPLOAD_TIMEOUT_MS);

    // Boundary für multipart
    String boundary = "----CardputerBoundary";
    String contentType = "multipart/form-data; boundary=" + boundary;

    // Dateiname aus Pfad extrahieren
    String filename = filePath.substring(filePath.lastIndexOf('/') + 1);

    // Header-Teil des multipart-Body
    String head = "--" + boundary + "\r\n"
                  "Content-Disposition: form-data; name=\"file\"; filename=\""
                  + filename + "\"\r\n"
                  "Content-Type: audio/wav\r\n\r\n";

    String tail = "\r\n--" + boundary + "--\r\n";

    uint32_t totalLen = head.length() + f.size() + tail.length();

    http.addHeader("Content-Type", contentType);
    http.addHeader("Content-Length", String(totalLen));

    // Stream-Upload
    int httpCode = http.sendRequest("POST", [&](WiFiClient& client) -> bool {
        client.print(head);

        uint8_t buf[512];
        while (f.available()) {
            int n = f.read(buf, sizeof(buf));
            if (n > 0) client.write(buf, n);
        }

        client.print(tail);
        return true;
    });

    f.close();

    if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_ACCEPTED) {
        String payload = http.getString();
        JsonDocument doc;
        if (deserializeJson(doc, payload) == DeserializationError::Ok) {
            jobId = doc["job_id"].as<String>();
        }
        http.end();
        return true;
    }

    http.end();
    return false;
}

// ── Offline-Queue ─────────────────────────────────────────────────────────────
void Uploader::addToQueue(const String& filePath) {
    File q = SD.open(QUEUE_FILE, FILE_APPEND);
    if (q) {
        q.println(filePath);
        q.close();
    }
}

void Uploader::processQueue() {
    if (!SD.exists(QUEUE_FILE)) return;

    File q = SD.open(QUEUE_FILE, FILE_READ);
    if (!q) return;

    std::vector<String> remaining;

    while (q.available()) {
        String line = q.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) continue;

        if (!SD.exists(line)) continue;  // Datei bereits gelöscht

        String jobId;
        if (upload(line, jobId)) {
            Serial.printf("[Queue] OK: %s -> %s\n", line.c_str(), jobId.c_str());
        } else {
            remaining.push_back(line);   // beim nächsten Versuch wieder versuchen
        }
    }
    q.close();

    // Queue-Datei mit verbleibenden Einträgen neu schreiben
    SD.remove(QUEUE_FILE);
    if (!remaining.empty()) {
        File qw = SD.open(QUEUE_FILE, FILE_WRITE);
        for (auto& p : remaining) {
            qw.println(p);
        }
        qw.close();
    }
}
