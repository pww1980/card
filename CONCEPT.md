# M5Stack Cardputer ADV – KI-Diktiergerät

## Überblick

Der M5Stack Cardputer ADV fungiert als mobiles Diktiergerät. Aufnahmen werden
automatisch per WLAN an einen lokalen Server übertragen. Dort läuft eine
vollautomatische Pipeline: Transkription (Whisper), optionale Sprechererkennung
(pyannote) und KI-Zusammenfassung (Ollama). Das Ergebnis ist eine strukturierte
Markdown-Datei. Optional wird später ein Web-Dashboard ergänzt.

---

## Systemarchitektur

```
┌─────────────────────────────────┐        WLAN / HTTP
│        Cardputer ADV            │ ──────────────────────►  ┌──────────────────────────────┐
│                                 │                           │         Ubuntu Server        │
│  Mikrofon → WAV (SD-Karte)     │  POST /upload             │                              │
│  LCD: Status-Anzeige           │ ◄──────────────────────── │  FastAPI  ──► Pipeline       │
│  Tastatur: Steuerung           │  JSON: { status, id }     │                              │
│  WiFi: HTTP-Client             │                           │  1. Whisper (Transkription)  │
└─────────────────────────────────┘                           │  2. pyannote (Sprecher)      │
                                                              │  3. Ollama (Zusammenfassung) │
                                                              │  4. Markdown-Export          │
                                                              │  5. Web-Dashboard (Phase 2)  │
                                                              └──────────────────────────────┘
```

---

## Komponenten

### 1. Cardputer ADV (PlatformIO / C++)

**Hardware (verifiziert, Quelle: M5Unified Quellcode):**
- MCU: ESP32-S3FN8 (Stamp-S3A Modul)
- Audio-Codec: **ES8311** (ersetzt PDM-Mikrofon der Standard-Version)
  - MEMS-Mikrofon mit hohem SNR
  - NS4150B Verstärker + 1W Lautsprecher
  - 3,5mm Klinkenausgang
- Display: 1,14" LCD, 240×135, ST7789
- MicroSD-Karte
- IMU: integriert (neu gegenüber Standard-Version)
- WiFi 802.11 b/g/n, verbesserte Antenne
- Akku: 1.750 mAh (größer als Standard 1.000 mAh)
- Tastatur: mechanisch, TCA8418 Controller

**Bestätigte GPIO-Pinbelegung:**
| Signal | GPIO | Anmerkung |
|---|---|---|
| I2S BCLK | 41 | Mic + Speaker |
| I2S WS/LRCLK | 43 | Mic + Speaker |
| I2S DOUT | 42 | → Speaker (DAC) |
| I2S DIN | 46 | ← Mikrofon (ADC) |
| I2S Port | I2S\_NUM\_1 | |
| ES8311 I2C SDA | 9 | I2C\_NUM\_1 |
| ES8311 I2C SCL | 8 | I2C\_NUM\_1, geteilt mit TCA8418 |
| ES8311 I2C Addr | 0x18 | |
| Ext. I2C SDA | 1 | Port A |
| Ext. I2C SCL | 2 | Port A |

**Aufnahme:**
- Format: WAV, Mono, 16 kHz, 16-bit PCM
  → Optimales Format für Whisper
- Speicherung auf SD-Karte (`/rec/YYYYMMDD_HHMMSS.wav`)
- Dateiname enthält Timestamp (RTC oder NTP-Zeit)

**Steuerung (Tastatur):**
| Taste | Aktion |
|-------|--------|
| `R`   | Aufnahme starten / stoppen |
| `U`   | Letzte Datei sofort hochladen |
| `L`   | Liste aufgenommener Dateien |
| `DEL` | Letzte Datei löschen |

**Automatik-Modus:**
- Nach Stop-Taste: automatischer Upload-Versuch
- Fehlgeschlagene Uploads werden in einer Queue auf SD gespeichert
- Retry beim nächsten WLAN-Connect

**LCD-Statusanzeigen:**
```
● REC  00:01:23          ← Aufnahme läuft
↑ Uploading...           ← Upload läuft
✓ Upload OK  #abc123     ← Server-Bestätigung mit Job-ID
✗ Upload FAIL (retry)    ← Fehlerfall
```

**Audio-Architektur (ADV vs. Standard):**
```
Standard Cardputer:   ESP32-S3 ── PDM ──► SPM1423 (Mikrofon, direkt)
                                           NS4168  (Speaker)

Cardputer ADV:        ESP32-S3 ── I2S ──► ES8311 Codec ──► MEMS-Mikrofon
                                  I2C ──► ES8311 Config        └──► Speaker (NS4150B)
```
→ Der ES8311 wird von M5Unified automatisch per I2C-Probe erkannt.
  `M5Cardputer.Mic.begin()` konfiguriert Codec und I2S korrekt ohne manuelle Pin-Angabe.

**Konfiguration (config.h):**
```cpp
#define WIFI_SSID     "NetzwerkName"
#define WIFI_PASS     "Passwort"
#define SERVER_HOST   "192.168.1.100"
#define SERVER_PORT   8000
#define SAMPLE_RATE   16000
#define BIT_DEPTH     16
```

---

### 2. Server (Python / Ubuntu)

#### 2.1 FastAPI REST-Endpunkte

```
POST /upload
  Body: multipart/form-data { file: audio.wav, meta: JSON }
  Response: { job_id, status: "queued" }

GET  /status/{job_id}
  Response: { job_id, status: "processing|done|failed", result_path }

GET  /results/{job_id}
  Response: Markdown-Datei (raw text)

GET  /results/
  Response: Liste aller verarbeiteten Jobs

GET  /health
  Response: { status: "ok", version, models_loaded }
```

#### 2.2 Verarbeitungs-Pipeline

```
audio.wav
    │
    ▼
┌─────────────────────────────┐
│  1. Whisper-Transkription   │  faster-whisper (CPU, int8)
│     Modell: medium / large  │  Sprache: auto (de/en)
│     → segments mit timestamps│
└─────────────────────────────┘
    │
    ▼
┌─────────────────────────────┐
│  2. Sprechererkennung       │  pyannote.audio 3.x
│     (optional, konfigurierbar│  → Speaker-Labels pro Segment
│      via ENV-Variable)      │
└─────────────────────────────┘
    │
    ▼
┌─────────────────────────────┐
│  3. Transkript mergen        │  Segmente zusammenführen
│     → annotiertes Transkript │  Sprecher + Text + Zeit
└─────────────────────────────┘
    │
    ▼
┌─────────────────────────────┐
│  4. LLM-Zusammenfassung     │  Ollama (lokal)
│     Modell: gemma3 / qwen3  │  Strukturierter Prompt → JSON
│     → Zusammenfassung,      │  → Themen, Stichworte,
│        Aufgaben, Keywords   │     Action Items
└─────────────────────────────┘
    │
    ▼
┌─────────────────────────────┐
│  5. Markdown-Formatter      │  Strukturierte .md-Datei
│     → output/{job_id}.md   │  + Roh-Transkript .txt
└─────────────────────────────┘
```

#### 2.3 Ausgabe-Format (Markdown)

```markdown
# Diktat – 2026-04-14 09:32

## Metadaten
| Feld        | Wert                  |
|-------------|----------------------|
| Job-ID      | abc123               |
| Datum       | 2026-04-14           |
| Uhrzeit     | 09:32:15             |
| Dauer       | 2:47 min             |
| Sprache     | Deutsch (de)         |
| Sprecher    | 2 erkannt            |
| Modell      | whisper/medium       |
| Zusammenfassung-Modell | gemma3:12b |

---

## Transkription

**[00:00]** Sprecher 1: Heute besprechen wir das neue Projekt...
**[00:14]** Sprecher 2: Ich denke, wir sollten zunächst...

---

## Zusammenfassung

Das Meeting behandelte die Planung des neuen Projekts. Es wurden
Verantwortlichkeiten verteilt und ein Zeitplan skizziert.

---

## Themen
- Projektplanung
- Ressourcenverteilung
- Zeitplan Q2 2026

## Action Items
- [ ] Sprecher 1: Angebot bis Freitag einholen
- [ ] Sprecher 2: Kick-off Termin koordinieren

## Schlüsselwörter
`Projekt`, `Planung`, `Q2`, `Budget`, `Kick-off`
```

---

## Technologie-Stack

| Komponente | Technologie | Begründung |
|---|---|---|
| Cardputer Firmware | PlatformIO / Arduino (C++) | Offizielle M5Stack-Unterstützung |
| Audio-Aufnahme | M5Stack PDM / I2S | Eingebautes Mikrofon ADV |
| HTTP-Client | Arduino HTTPClient | WiFi-Upload |
| Server-Framework | FastAPI (Python) | Async, einfaches API-Design |
| Aufgaben-Queue | asyncio / BackgroundTasks | Kein Overhead, reicht für CPU-Pipeline |
| Transkription | faster-whisper | CPU-optimiert (int8/float16), schneller als openai-whisper |
| Sprechererkennung | pyannote.audio 3.x | State-of-the-art, lokal ausführbar |
| LLM | Ollama + gemma3:12b oder qwen3:8b | Lokal, kein Cloud-Zugriff nötig |
| Ausgabe | Markdown (.md) + Plain-Text (.txt) | Portabel, versionierbar |
| Dashboard (Phase 2) | FastAPI + Jinja2 oder separates SPA | Einfache Übersicht aller Diktate |

---

## Verzeichnisstruktur

```
card/
├── CONCEPT.md                     ← Dieses Dokument
│
├── cardputer/                     ← PlatformIO-Projekt
│   ├── platformio.ini
│   ├── include/
│   │   └── config.h               ← WiFi, Server, Audio-Parameter
│   ├── src/
│   │   ├── main.cpp               ← Hauptprogramm
│   │   ├── recorder.h/.cpp        ← PDM → WAV-Aufnahme
│   │   ├── uploader.h/.cpp        ← HTTP multipart Upload
│   │   ├── display.h/.cpp         ← LCD-UI
│   │   └── queue.h/.cpp           ← Offline-Queue auf SD
│   └── lib/                       ← Ggf. lokale Libraries
│
└── server/                        ← Python-Server
    ├── requirements.txt
    ├── main.py                    ← FastAPI App + Endpunkte
    ├── pipeline/
    │   ├── __init__.py
    │   ├── transcribe.py          ← Whisper-Integration
    │   ├── diarize.py             ← pyannote-Integration
    │   ├── summarize.py           ← Ollama-Integration
    │   └── formatter.py           ← Markdown-Ausgabe
    ├── models/                    ← Gecachte Modelle (Whisper)
    ├── uploads/                   ← Eingehende Audio-Dateien
    ├── output/                    ← Fertige Markdown-Dateien
    └── dashboard/                 ← (Phase 2) Web-UI
        ├── templates/
        └── static/
```

---

## Konfiguration Server

Umgebungsvariablen (`.env`):

```env
# Server
HOST=0.0.0.0
PORT=8000

# Whisper
WHISPER_MODEL=medium          # tiny / base / small / medium / large-v3
WHISPER_DEVICE=cpu
WHISPER_COMPUTE_TYPE=int8     # Optimal für CPU

# Sprechererkennung
DIARIZATION_ENABLED=true
HF_TOKEN=hf_...               # HuggingFace-Token für pyannote

# Ollama
OLLAMA_HOST=http://localhost:11434
OLLAMA_MODEL=gemma3:12b       # oder qwen3:8b

# Pfade
UPLOAD_DIR=./uploads
OUTPUT_DIR=./output
```

---

## Phasen / Roadmap

### Phase 1 – MVP (Kern-Funktionalität)
- [x] Konzept & Architektur
- [ ] Cardputer: Aufnahme (WAV, 16kHz) auf SD
- [ ] Cardputer: WLAN-Upload mit Status-Anzeige
- [ ] Server: FastAPI mit `/upload` und `/status`
- [ ] Server: Whisper-Transkription (ohne Diarization)
- [ ] Server: Ollama-Zusammenfassung
- [ ] Server: Markdown-Ausgabe

### Phase 2 – Sprechererkennung
- [ ] pyannote.audio Integration
- [ ] Speaker-Labels im Transkript
- [ ] Merge-Logik Transkription + Diarization

### Phase 3 – Web-Dashboard
- [ ] Liste aller Diktate
- [ ] Volltextsuche
- [ ] Audio-Playback im Browser
- [ ] Markdown-Vorschau

### Phase 4 – Erweiterungen
- [ ] Cardputer: Offline-Queue mit Retry
- [ ] Export: PDF, DOCX
- [ ] Webhook / Benachrichtigung (z.B. ntfy.sh)
- [ ] Automatische Stille-Erkennung für Auto-Stop

---

## Offene Entscheidungen

| Thema | Option A | Option B | Empfehlung |
|---|---|---|---|
| Whisper-Modell | `medium` (769M, ~15min/h Audio CPU) | `large-v3` (langsamer) | `medium` für CPU-Start |
| LLM | `gemma3:12b` | `qwen3:8b` | `qwen3:8b` leichter für CPU |
| Diarization | pyannote (HF-Token nötig) | Nur Whisper-Segmente | Phase 1: optional |
| Dashboard | FastAPI + Jinja2 | Separates SPA (Vue/React) | Jinja2 für Einfachheit |
| Queue Cardputer | In-Memory | SD-Datei | SD für Persistenz |
