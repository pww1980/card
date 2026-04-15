#!/bin/bash
# Server-Setup für das Cardputer-Diktiergerät
# Einmalig ausführen: bash setup.sh

set -e
cd "$(dirname "$0")"

echo "=== Cardputer-Server Setup ==="

# ── 1. Python-Umgebung ────────────────────────────────────────────────────────
echo ""
echo "[1/6] Python Virtual Environment..."
python3 -m venv .venv
source .venv/bin/activate
pip install --upgrade pip -q

echo "[2/6] Python-Abhängigkeiten installieren..."
pip install -r requirements.txt -q
echo "      OK"

# ── 2. .env anlegen ───────────────────────────────────────────────────────────
echo ""
echo "[3/6] Konfiguration..."
if [ ! -f .env ]; then
    cp .env.example .env
    echo "      .env angelegt – bitte HF_TOKEN eintragen!"
else
    echo "      .env existiert bereits – übersprungen"
fi

# ── 3. Whisper-Modell vorab herunterladen ─────────────────────────────────────
echo ""
echo "[4/6] Whisper-Modell herunterladen..."
source .env 2>/dev/null || true
MODEL=${WHISPER_MODEL:-medium}
python3 - <<EOF
from faster_whisper import WhisperModel
print("  Lade Modell '$MODEL'...")
WhisperModel("$MODEL", device="cpu", compute_type="int8")
print("  Whisper-Modell bereit.")
EOF

# ── 4. Pyannote-Sprechererkennung vorab herunterladen ────────────────────────
echo ""
echo "[5/6] Sprechererkennung (pyannote)..."
source .env 2>/dev/null || true
DIARIZATION_ENABLED=${DIARIZATION_ENABLED:-false}
HF_TOKEN=${HF_TOKEN:-}

if [ "$DIARIZATION_ENABLED" = "true" ] && [ -n "$HF_TOKEN" ] && [ "$HF_TOKEN" != "hf_DEIN_TOKEN_HIER" ]; then
    python3 - <<PYEOF
import os
os.environ["HF_TOKEN"] = "$HF_TOKEN"
print("  Lade pyannote/speaker-diarization-3.1 ...")
try:
    from pyannote.audio import Pipeline
    Pipeline.from_pretrained(
        "pyannote/speaker-diarization-3.1",
        use_auth_token="$HF_TOKEN",
    )
    print("  Diarization-Pipeline bereit.")
except Exception as e:
    print(f"  FEHLER: {e}")
    print("  Bitte sicherstellen dass:")
    print("    1. HF_TOKEN in .env korrekt gesetzt ist")
    print("    2. Modell-Zugriff auf HuggingFace beantragt wurde:")
    print("       https://huggingface.co/pyannote/speaker-diarization-3.1")
    print("       https://huggingface.co/pyannote/segmentation-3.0")
PYEOF
elif [ "$DIARIZATION_ENABLED" = "true" ] && { [ -z "$HF_TOKEN" ] || [ "$HF_TOKEN" = "hf_DEIN_TOKEN_HIER" ]; }; then
    echo "  DIARIZATION_ENABLED=true, aber HF_TOKEN fehlt."
    echo ""
    echo "  So einrichten:"
    echo "    1. HuggingFace-Account: https://huggingface.co"
    echo "    2. Token erstellen:     https://huggingface.co/settings/tokens"
    echo "    3. Modell-Zugriff beantragen:"
    echo "       https://huggingface.co/pyannote/speaker-diarization-3.1"
    echo "       https://huggingface.co/pyannote/segmentation-3.0"
    echo "    4. HF_TOKEN=hf_... in server/.env eintragen"
    echo "    5. setup.sh erneut ausführen"
else
    echo "  Deaktiviert (DIARIZATION_ENABLED=false)"
fi

# ── 5. Ollama prüfen und Modell laden ────────────────────────────────────────
echo ""
echo "[6/6] Ollama..."
if command -v ollama &>/dev/null; then
    source .env 2>/dev/null || true
    OLLAMA_MODEL_NAME=${OLLAMA_MODEL:-qwen3:8b}
    echo "  Lade Ollama-Modell: $OLLAMA_MODEL_NAME"
    ollama pull "$OLLAMA_MODEL_NAME"
else
    echo "  Ollama nicht gefunden. Bitte installieren:"
    echo "  curl -fsSL https://ollama.com/install.sh | sh"
    echo "  Danach: ollama pull qwen3:8b"
fi

# ── 6. Verzeichnisse anlegen ─────────────────────────────────────────────────
mkdir -p uploads output

echo ""
echo "=== Setup abgeschlossen ==="
echo ""
echo "Server starten:"
echo "  source .venv/bin/activate"
echo "  python main.py"
echo ""
echo "Oder als systemd-Service:"
echo "  sudo cp cardputer-server.service /etc/systemd/system/"
echo "  sudo systemctl enable --now cardputer-server"
