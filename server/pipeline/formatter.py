"""
Markdown-Formatter: erzeugt die strukturierte .md-Ausgabedatei.
"""

from datetime import datetime
from pathlib import Path


def format_markdown(
    output_path: Path,
    job_id: str,
    audio_path: Path,
    segments: list[dict],
    language: str,
    duration: float,
    summary: dict,
    diarization_used: bool,
    ollama_error: str | None = None,
) -> None:
    """Schreibt die strukturierte Markdown-Datei."""

    now      = datetime.now()
    date_str = now.strftime("%Y-%m-%d")
    time_str = now.strftime("%H:%M:%S")

    speaker_count = len(set(s.get("speaker") for s in segments if s.get("speaker")))
    lang_label    = {"de": "Deutsch", "en": "Englisch"}.get(language, language)
    dur_str       = _fmt_duration(duration)

    lines = []

    # ── Titel ─────────────────────────────────────────────────────────────────
    lines.append(f"# Diktat – {date_str} {time_str[:5]}\n")

    # ── Metadaten ─────────────────────────────────────────────────────────────
    lines.append("## Metadaten\n")
    lines.append("| Feld | Wert |")
    lines.append("|---|---|")
    lines.append(f"| Job-ID | `{job_id}` |")
    lines.append(f"| Datum | {date_str} |")
    lines.append(f"| Uhrzeit | {time_str} |")
    lines.append(f"| Audiodatei | `{audio_path.name}` |")
    lines.append(f"| Dauer | {dur_str} |")
    lines.append(f"| Sprache | {lang_label} |")
    lines.append(f"| Sprecher | {speaker_count} erkannt |")
    lines.append(f"| Sprechererkennung | {'ja' if diarization_used else 'nein'} |")
    lines.append(f"| Stimmung | {summary.get('stimmung', '-')} |")
    if ollama_error:
        lines.append(f"| Zusammenfassung | ⚠ Ollama-Fehler |")
    lines.append("")

    # ── Zusammenfassung ───────────────────────────────────────────────────────
    lines.append("---\n")
    lines.append("## Zusammenfassung\n")
    lines.append(summary.get("zusammenfassung", "_keine Zusammenfassung verfügbar_"))
    lines.append("")

    # ── Themen ───────────────────────────────────────────────────────────────
    themen = summary.get("themen", [])
    if themen:
        lines.append("## Themen\n")
        for t in themen:
            lines.append(f"- {t}")
        lines.append("")

    # ── Action Items ─────────────────────────────────────────────────────────
    actions = summary.get("action_items", [])
    if actions:
        lines.append("## Action Items\n")
        for a in actions:
            lines.append(f"- [ ] {a}")
        lines.append("")

    # ── Stichworte ────────────────────────────────────────────────────────────
    keywords = summary.get("stichworte", [])
    if keywords:
        lines.append("## Stichworte\n")
        lines.append(" ".join(f"`{k}`" for k in keywords))
        lines.append("")

    # ── Transkription ─────────────────────────────────────────────────────────
    lines.append("---\n")
    lines.append("## Transkription\n")
    for seg in segments:
        ts      = _fmt_time(seg["start"])
        speaker = seg.get("speaker") or "?"
        text    = seg.get("text", "")
        lines.append(f"**[{ts}]** {speaker}: {text}")
    lines.append("")

    output_path.write_text("\n".join(lines), encoding="utf-8")


def save_transcript(
    output_path: Path,
    job_id: str,
    audio_path: Path,
    segments: list[dict],
    language: str,
    duration: float,
) -> None:
    """Speichert nur das Transkript (ohne Zusammenfassung) als Markdown."""
    now      = datetime.now()
    date_str = now.strftime("%Y-%m-%d")
    time_str = now.strftime("%H:%M:%S")
    lang_label = {"de": "Deutsch", "en": "Englisch"}.get(language, language)

    lines = [
        f"# Transkript – {date_str} {time_str[:5]}\n",
        "## Metadaten\n",
        "| Feld | Wert |", "|---|---|",
        f"| Job-ID | `{job_id}` |",
        f"| Datum | {date_str} |",
        f"| Uhrzeit | {time_str} |",
        f"| Audiodatei | `{audio_path.name}` |",
        f"| Dauer | {_fmt_duration(duration)} |",
        f"| Sprache | {lang_label} |",
        "",
        "---\n",
        "## Transkription\n",
    ]
    for seg in segments:
        ts      = _fmt_time(seg["start"])
        speaker = seg.get("speaker") or "?"
        text    = seg.get("text", "")
        lines.append(f"**[{ts}]** {speaker}: {text}")
    lines.append("")
    output_path.write_text("\n".join(lines), encoding="utf-8")


def _fmt_duration(seconds: float) -> str:
    m = int(seconds) // 60
    s = int(seconds) % 60
    return f"{m}:{s:02d} min"


def _fmt_time(seconds: float) -> str:
    m = int(seconds) // 60
    s = int(seconds) % 60
    return f"{m:02d}:{s:02d}"
