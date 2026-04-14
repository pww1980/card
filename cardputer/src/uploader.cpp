#include "uploader.h"
#include "config.h"

#include <WiFi.h>
#include <SD.h>
#include <ArduinoJson.h>
#include <vector>

// ── Multipart-Upload via raw WiFiClient ───────────────────────────────────────
// HTTPClient unterstützt kein Streaming großer Dateien – daher direktes TCP.
bool Uploader::upload(const String& filePath, String& jobId) {
    File f = SD.open(filePath, FILE_READ);
    if (!f) {
        Serial.println("[Upload] Datei nicht gefunden: " + filePath);
        return false;
    }

    String filename  = filePath.substring(filePath.lastIndexOf('/') + 1);
    String boundary  = "CardputerBnd";

    String partHead = "--" + boundary + "\r\n"
                      "Content-Disposition: form-data; name=\"file\"; "
                      "filename=\"" + filename + "\"\r\n"
                      "Content-Type: audio/wav\r\n\r\n";
    String partTail = "\r\n--" + boundary + "--\r\n";

    uint32_t bodyLen = partHead.length() + (uint32_t)f.size() + partTail.length();

    // ── Verbinden ─────────────────────────────────────────────────────────────
    WiFiClient client;
    client.setTimeout(UPLOAD_TIMEOUT_MS / 1000);
    if (!client.connect(SERVER_HOST, SERVER_PORT)) {
        Serial.println("[Upload] Verbindung fehlgeschlagen");
        f.close();
        return false;
    }

    // ── HTTP-Request senden ───────────────────────────────────────────────────
    client.printf("POST %s HTTP/1.1\r\n",   UPLOAD_PATH);
    client.printf("Host: %s:%d\r\n",        SERVER_HOST, SERVER_PORT);
    client.printf("Content-Type: multipart/form-data; boundary=%s\r\n",
                  boundary.c_str());
    client.printf("Content-Length: %u\r\n", bodyLen);
    client.print("Connection: close\r\n\r\n");

    // ── Body streamen ─────────────────────────────────────────────────────────
    client.print(partHead);

    uint8_t buf[512];
    while (f.available()) {
        int n = f.read(buf, sizeof(buf));
        if (n > 0) client.write(buf, n);
    }
    f.close();

    client.print(partTail);

    // ── Response lesen ────────────────────────────────────────────────────────
    unsigned long deadline = millis() + UPLOAD_TIMEOUT_MS;
    while (client.available() == 0 && millis() < deadline) delay(10);

    // Status-Zeile: "HTTP/1.1 202 Accepted"
    String statusLine = client.readStringUntil('\n');
    int httpCode = 0;
    if (statusLine.length() > 12) {
        httpCode = statusLine.substring(9, 12).toInt();
    }

    // Header-Block überspringen
    while (client.available()) {
        String line = client.readStringUntil('\n');
        if (line == "\r" || line.isEmpty()) break;
    }

    // Body (JSON) lesen
    String body;
    while (client.available()) body += (char)client.read();
    client.stop();

    Serial.printf("[Upload] HTTP %d  body: %s\n", httpCode, body.c_str());

    if (httpCode == 200 || httpCode == 202) {
        JsonDocument doc;
        if (deserializeJson(doc, body) == DeserializationError::Ok) {
            jobId = doc["job_id"].as<String>();
        }
        return true;
    }
    return false;
}

// ── Offline-Queue ─────────────────────────────────────────────────────────────
void Uploader::addToQueue(const String& filePath) {
    File q = SD.open(QUEUE_FILE, FILE_APPEND);
    if (q) {
        q.println(filePath);
        q.close();
        Serial.println("[Queue] Hinzugefügt: " + filePath);
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
        if (line.isEmpty()) continue;
        if (!SD.exists(line)) continue;  // Datei gelöscht → überspringen

        String jobId;
        if (upload(line, jobId)) {
            Serial.printf("[Queue] OK: %s → %s\n", line.c_str(), jobId.c_str());
        } else {
            remaining.push_back(line);
        }
    }
    q.close();

    SD.remove(QUEUE_FILE);
    if (!remaining.empty()) {
        File qw = SD.open(QUEUE_FILE, FILE_WRITE);
        for (auto& p : remaining) qw.println(p);
        qw.close();
    }
}
