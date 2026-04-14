#!/bin/bash
# Server-Setup für das Cardputer-Diktiergerät
# Einmalig ausführen: bash setup.sh

set -e
cd "$(dirname "$0")"

echo "=== Cardputer-Server Setup ==="

# ── 1. Python-Umgebung ────────────────────────────────────────────────────────
echo ""
echo "[1/5] Python Virtual Environment..."
python3 -m venv .venv
source .venv/bin/activate
pip install --upgrade pip -q

echo "[2/5] Python-Abhängigkeiten installieren..."
pip install -r requirements.txt -q
echo "      OK"

# ── 2. .env anlegen ───────────────────────────────────────────────────────────
echo ""
echo "[3/5] Konfiguration..."
if [ ! -f .env ]; then
    cp .env.example .env
    echo "      .env angelegt – bitte anpassen (Modell, HF_TOKEN, etc.)"
else
    echo "      .env existiert bereits – übersprungen"
fi

# ── 3. Whisper-Modell vorab herunterladen ─────────────────────────────────────
echo ""
echo "[4/5] Whisper-Modell herunterladen..."
source .env 2>/dev/null || true
MODEL=${WHISPER_MODEL:-medium}
python3 - <<EOF
from faster_whisper import WhisperModel
print(f"  Lade Modell '{os.environ.get(\"WHISPER_MODEL\", \"medium\")}'...")
import os
WhisperModel(os.environ.get("WHISPER_MODEL", "medium"),
             device="cpu", compute_type="int8")
print("  Whisper-Modell bereit.")
EOF

# ── 4. Ollama prüfen und Modell laden ────────────────────────────────────────
echo ""
echo "[5/5] Ollama..."
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

# ── 5. Verzeichnisse anlegen ─────────────────────────────────────────────────
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
