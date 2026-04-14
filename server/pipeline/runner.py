"""
Pipeline-Koordinator: ruft Transkription, Diarization und Zusammenfassung
nacheinander auf und erstellt die Markdown-Ausgabe.
"""

import os
from pathlib import Path

from .transcribe import transcribe
from .diarize    import diarize, merge_diarization
from .summarize  import summarize
from .formatter  import format_markdown


def run_pipeline(audio_path: Path, output_dir: Path, job_id: str) -> Path:
    """
    Vollständige Verarbeitungs-Pipeline.
    Gibt den Pfad zur erzeugten Markdown-Datei zurück.
    """
    print(f"[{job_id}] Pipeline gestartet: {audio_path}")

    # 1. Transkription (Whisper)
    print(f"[{job_id}] Transkribiere...")
    segments, language, duration = transcribe(audio_path)

    # 2. Sprechererkennung (optional)
    diarization_enabled = os.getenv("DIARIZATION_ENABLED", "false").lower() == "true"
    if diarization_enabled:
        print(f"[{job_id}] Sprechererkennung...")
        speaker_segments = diarize(audio_path)
        segments = merge_diarization(segments, speaker_segments)
    else:
        # Kein Sprecher-Label → Sprecher 1 für alle Segmente
        for seg in segments:
            seg["speaker"] = "Sprecher 1"

    # 3. Zusammenfassung (Ollama)
    print(f"[{job_id}] Zusammenfassung generieren...")
    summary = summarize(segments, language)

    # 4. Markdown-Datei erzeugen
    print(f"[{job_id}] Markdown schreiben...")
    output_path = output_dir / f"{job_id}.md"
    format_markdown(
        output_path=output_path,
        job_id=job_id,
        audio_path=audio_path,
        segments=segments,
        language=language,
        duration=duration,
        summary=summary,
        diarization_used=diarization_enabled,
    )

    print(f"[{job_id}] Fertig: {output_path}")
    return output_path
