"""
Sprechererkennung via pyannote.audio.
Benötigt einen HuggingFace-Token (HF_TOKEN in .env).
Wird nur verwendet wenn DIARIZATION_ENABLED=true.
"""

import os
from pathlib import Path

_pipeline = None


def _get_pipeline():
    global _pipeline
    if _pipeline is None:
        from pyannote.audio import Pipeline
        hf_token = os.getenv("HF_TOKEN")
        if not hf_token:
            raise RuntimeError(
                "HF_TOKEN fehlt. Bitte in .env setzen.\n"
                "Token unter https://huggingface.co/settings/tokens erstellen.\n"
                "Zugriff auf pyannote/speaker-diarization-3.1 beantragen."
            )
        print("[pyannote] Lade Diarization-Pipeline...")
        _pipeline = Pipeline.from_pretrained(
            "pyannote/speaker-diarization-3.1",
            use_auth_token=hf_token,
        )
        print("[pyannote] Pipeline geladen.")
    return _pipeline


def diarize(audio_path: Path) -> list[dict]:
    """
    Gibt eine Liste von Speaker-Segmenten zurück:
    [ { start, end, speaker }, ... ]
    """
    pipeline = _get_pipeline()
    diarization = pipeline(str(audio_path))

    segments = []
    for turn, _, speaker in diarization.itertracks(yield_label=True):
        segments.append({
            "start":   round(turn.start, 2),
            "end":     round(turn.end,   2),
            "speaker": speaker,
        })

    print(f"[pyannote] {len(set(s['speaker'] for s in segments))} Sprecher erkannt, "
          f"{len(segments)} Segmente")
    return segments


def merge_diarization(
    transcription_segments: list[dict],
    speaker_segments: list[dict],
) -> list[dict]:
    """
    Weist jedem Transkriptions-Segment den dominanten Sprecher zu,
    basierend auf Überlapp-Zeit.
    """
    for ts in transcription_segments:
        t_start, t_end = ts["start"], ts["end"]
        best_speaker  = "Sprecher ?"
        best_overlap  = 0.0

        for ss in speaker_segments:
            overlap = min(t_end, ss["end"]) - max(t_start, ss["start"])
            if overlap > best_overlap:
                best_overlap  = overlap
                best_speaker  = ss["speaker"]

        # Lesbare Labels: SPEAKER_00 → Sprecher 1
        label = best_speaker
        if best_speaker.startswith("SPEAKER_"):
            num = int(best_speaker.split("_")[1]) + 1
            label = f"Sprecher {num}"

        ts["speaker"] = label

    return transcription_segments
