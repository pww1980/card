"""
LLM-Zusammenfassung via Ollama (lokal).
Modell: gemma3:12b oder qwen3:8b (konfigurierbar per ENV).
"""

import os
import json
import re

import ollama


SYSTEM_PROMPT = """Du bist ein KI-Assistent, der Sprachaufnahmen auswertet.
Antworte ausschließlich mit validem JSON. Kein Markdown, kein Fließtext außerhalb des JSON.
Verwende die Sprache der Transkription für alle Textfelder."""

USER_PROMPT_TEMPLATE = """Analysiere folgendes Transkript einer Sprachaufnahme.
Sprache: {language}

TRANSKRIPT:
{transcript}

Erstelle eine strukturierte Auswertung als JSON mit folgenden Feldern:
{{
  "zusammenfassung": "2-4 Sätze, die den Inhalt zusammenfassen",
  "themen": ["Thema 1", "Thema 2"],
  "action_items": ["Aufgabe 1 (zuständig: Name/Sprecher)", "Aufgabe 2"],
  "stichworte": ["Keyword1", "Keyword2", "Keyword3"],
  "stimmung": "neutral | positiv | negativ | gemischt",
  "sprache": "{language}"
}}

Wenn keine Action Items erkennbar sind, setze action_items auf [].
"""


def summarize(segments: list[dict], language: str) -> dict:
    """
    Sendet das Transkript an Ollama und gibt strukturierte Zusammenfassung zurück.
    """
    model = os.getenv("OLLAMA_MODEL", "qwen3:8b")
    host  = os.getenv("OLLAMA_HOST",  "http://localhost:11434")

    # Transkript als Text zusammenbauen
    transcript_lines = []
    for seg in segments:
        speaker = seg.get("speaker") or "?"
        transcript_lines.append(f"[{_fmt_time(seg['start'])}] {speaker}: {seg['text']}")
    transcript = "\n".join(transcript_lines)

    lang_label = "Deutsch" if language == "de" else "Englisch" if language == "en" else language

    prompt = USER_PROMPT_TEMPLATE.format(
        language=lang_label,
        transcript=transcript,
    )

    client = ollama.Client(host=host)

    print(f"[Ollama] Sende Anfrage an Modell '{model}'...")
    response = client.chat(
        model=model,
        messages=[
            {"role": "system", "content": SYSTEM_PROMPT},
            {"role": "user",   "content": prompt},
        ],
        options={"temperature": 0.3},
    )

    raw = response.message.content.strip()

    # <think>…</think> Reasoning-Blöcke entfernen (qwen3, deepseek-r1, o-Modelle)
    raw = re.sub(r"<think>.*?</think>", "", raw, flags=re.DOTALL).strip()

    # JSON aus Antwort extrahieren (LLM gibt manchmal Markdown-Blöcke zurück)
    if "```" in raw:
        raw = raw.split("```")[1]
        if raw.startswith("json"):
            raw = raw[4:]
        raw = raw.strip()

    try:
        result = json.loads(raw)
    except json.JSONDecodeError:
        print(f"[Ollama] JSON-Parse-Fehler, Fallback:\n{raw}")
        result = {
            "zusammenfassung": raw[:500],
            "themen":          [],
            "action_items":    [],
            "stichworte":      [],
            "stimmung":        "neutral",
            "sprache":         language,
        }

    print(f"[Ollama] Zusammenfassung: {result.get('zusammenfassung', '')[:80]}...")
    return result


def _fmt_time(seconds: float) -> str:
    m = int(seconds) // 60
    s = int(seconds) % 60
    return f"{m:02d}:{s:02d}"
