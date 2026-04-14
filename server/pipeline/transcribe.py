"""
Whisper-Transkription via faster-whisper.
Unterstützt Deutsch und Englisch (auto-detect).
"""

import os
from pathlib import Path

from faster_whisper import WhisperModel

_model: WhisperModel | None = None


def _get_model() -> WhisperModel:
    global _model
    if _model is None:
        model_size    = os.getenv("WHISPER_MODEL", "medium")
        device        = os.getenv("WHISPER_DEVICE", "cpu")
        compute_type  = os.getenv("WHISPER_COMPUTE_TYPE", "int8")
        print(f"[Whisper] Lade Modell '{model_size}' ({device}/{compute_type})...")
        _model = WhisperModel(model_size, device=device, compute_type=compute_type)
        print("[Whisper] Modell geladen.")
    return _model


def transcribe(audio_path: Path) -> tuple[list[dict], str, float]:
    """
    Transkribiert eine WAV-Datei.

    Rückgabe:
        segments: Liste von { start, end, text, speaker }
        language: erkannte Sprache ('de', 'en', ...)
        duration: Audiodauer in Sekunden
    """
    model = _get_model()

    segments_gen, info = model.transcribe(
        str(audio_path),
        language=None,           # auto-detect
        beam_size=5,
        vad_filter=True,         # Stille herausfiltern
        vad_parameters=dict(min_silence_duration_ms=500),
    )

    language = info.language
    duration = info.duration

    segments = []
    for seg in segments_gen:
        segments.append({
            "start":   round(seg.start, 2),
            "end":     round(seg.end,   2),
            "text":    seg.text.strip(),
            "speaker": None,  # wird ggf. von Diarization befüllt
        })

    print(f"[Whisper] Sprache: {language}, Dauer: {duration:.1f}s, "
          f"Segmente: {len(segments)}")
    return segments, language, duration
