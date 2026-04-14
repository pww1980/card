"""
M5Stack Cardputer – Diktiergerät Server
FastAPI-App: empfängt Audio-Uploads und startet die Verarbeitungs-Pipeline.
"""

import os
import uuid
import asyncio
from pathlib import Path
from typing import Optional

import aiofiles
from dotenv import load_dotenv
from fastapi import FastAPI, File, UploadFile, HTTPException, BackgroundTasks
from fastapi.responses import FileResponse, PlainTextResponse

from pipeline import run_pipeline

load_dotenv()

UPLOAD_DIR = Path(os.getenv("UPLOAD_DIR", "./uploads"))
OUTPUT_DIR = Path(os.getenv("OUTPUT_DIR", "./output"))

UPLOAD_DIR.mkdir(parents=True, exist_ok=True)
OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

app = FastAPI(title="Cardputer Diktiergerät", version="0.1.0")

# In-Memory Job-Status (für Produktion: Redis/SQLite)
jobs: dict[str, dict] = {}


# ── Endpunkte ─────────────────────────────────────────────────────────────────

@app.get("/health")
async def health():
    return {"status": "ok", "version": "0.1.0"}


@app.post("/upload", status_code=202)
async def upload_audio(
    background_tasks: BackgroundTasks,
    file: UploadFile = File(...),
):
    """Empfängt eine WAV-Datei und startet die Pipeline im Hintergrund."""
    if not file.filename or not file.filename.endswith(".wav"):
        raise HTTPException(400, "Nur WAV-Dateien werden akzeptiert")

    job_id = uuid.uuid4().hex[:8]
    audio_path = UPLOAD_DIR / f"{job_id}_{file.filename}"

    # Datei speichern
    async with aiofiles.open(audio_path, "wb") as f:
        content = await file.read()
        await f.write(content)

    # Job registrieren
    jobs[job_id] = {
        "job_id":     job_id,
        "status":     "queued",
        "audio_file": str(audio_path),
        "result":     None,
        "error":      None,
    }

    # Pipeline asynchron starten
    background_tasks.add_task(_run_job, job_id, audio_path)

    return {"job_id": job_id, "status": "queued"}


@app.get("/status/{job_id}")
async def job_status(job_id: str):
    """Gibt den aktuellen Status eines Jobs zurück."""
    if job_id not in jobs:
        raise HTTPException(404, "Job nicht gefunden")
    return jobs[job_id]


@app.get("/results/{job_id}")
async def get_result(job_id: str):
    """Liefert die fertige Markdown-Datei zurück."""
    job = jobs.get(job_id)
    if not job:
        raise HTTPException(404, "Job nicht gefunden")
    if job["status"] != "done":
        raise HTTPException(409, f"Job noch nicht fertig: {job['status']}")
    result_path = Path(job["result"])
    if not result_path.exists():
        raise HTTPException(500, "Ergebnis-Datei fehlt")
    return FileResponse(result_path, media_type="text/markdown")


@app.get("/results/")
async def list_results():
    """Listet alle abgeschlossenen Jobs."""
    return [
        {"job_id": jid, "status": j["status"], "result": j["result"]}
        for jid, j in jobs.items()
    ]


# ── Hintergrundverarbeitung ───────────────────────────────────────────────────

async def _run_job(job_id: str, audio_path: Path):
    jobs[job_id]["status"] = "processing"
    try:
        output_path = await asyncio.to_thread(
            run_pipeline,
            audio_path=audio_path,
            output_dir=OUTPUT_DIR,
            job_id=job_id,
        )
        jobs[job_id]["status"] = "done"
        jobs[job_id]["result"] = str(output_path)
    except Exception as exc:
        jobs[job_id]["status"] = "failed"
        jobs[job_id]["error"]  = str(exc)
        raise


# ── Start ─────────────────────────────────────────────────────────────────────

if __name__ == "__main__":
    import uvicorn
    uvicorn.run(
        "main:app",
        host=os.getenv("HOST", "0.0.0.0"),
        port=int(os.getenv("PORT", 8000)),
        reload=False,
    )
