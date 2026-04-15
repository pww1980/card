"""
M5Stack Cardputer – Diktiergerät Server
FastAPI-App: empfängt Audio-Uploads und startet die Verarbeitungs-Pipeline.
Jobs werden in SQLite persistiert – überleben Server-Neustarts.
"""

import os
import uuid
import sqlite3
import asyncio
from pathlib import Path
from contextlib import contextmanager
from datetime import datetime

import aiofiles
from dotenv import load_dotenv
from fastapi import FastAPI, File, UploadFile, HTTPException, BackgroundTasks
from fastapi.responses import FileResponse

from pipeline import run_pipeline

load_dotenv()

UPLOAD_DIR      = Path(os.getenv("UPLOAD_DIR",    "./uploads"))
OUTPUT_DIR      = Path(os.getenv("OUTPUT_DIR",    "./output"))
DB_PATH         = Path(os.getenv("DB_PATH",       "./jobs.db"))
MAX_UPLOAD_MB   = int(os.getenv("MAX_UPLOAD_MB",  "200"))

UPLOAD_DIR.mkdir(parents=True, exist_ok=True)
OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

app = FastAPI(title="Cardputer Diktiergerät", version="0.2.0")


# ── SQLite-Hilfsfunktionen ────────────────────────────────────────────────────

def _init_db() -> None:
    with sqlite3.connect(DB_PATH) as conn:
        conn.execute("""
            CREATE TABLE IF NOT EXISTS jobs (
                job_id      TEXT PRIMARY KEY,
                status      TEXT NOT NULL DEFAULT 'queued',
                audio_file  TEXT,
                transcript  TEXT,
                result      TEXT,
                error       TEXT,
                created_at  TEXT DEFAULT (datetime('now','localtime'))
            )
        """)
        # Migration für bestehende Datenbanken ohne transcript-Spalte
        try:
            conn.execute("ALTER TABLE jobs ADD COLUMN transcript TEXT")
        except sqlite3.OperationalError:
            pass  # Spalte existiert bereits

def _db_save(job: dict) -> None:
    with sqlite3.connect(DB_PATH) as conn:
        conn.execute("""
            INSERT INTO jobs (job_id, status, audio_file, transcript, result, error)
            VALUES (:job_id, :status, :audio_file, :transcript, :result, :error)
            ON CONFLICT(job_id) DO UPDATE SET
                status     = excluded.status,
                audio_file = excluded.audio_file,
                transcript = COALESCE(excluded.transcript, transcript),
                result     = excluded.result,
                error      = excluded.error
        """, {
            "job_id":     job["job_id"],
            "status":     job["status"],
            "audio_file": job.get("audio_file"),
            "transcript": job.get("transcript"),
            "result":     job.get("result"),
            "error":      job.get("error"),
        })

def _db_get(job_id: str) -> dict | None:
    with sqlite3.connect(DB_PATH) as conn:
        conn.row_factory = sqlite3.Row
        row = conn.execute(
            "SELECT * FROM jobs WHERE job_id = ?", (job_id,)
        ).fetchone()
        return dict(row) if row else None

def _db_list() -> list[dict]:
    with sqlite3.connect(DB_PATH) as conn:
        conn.row_factory = sqlite3.Row
        rows = conn.execute(
            "SELECT * FROM jobs ORDER BY created_at DESC"
        ).fetchall()
        return [dict(r) for r in rows]


# ── App-Start ─────────────────────────────────────────────────────────────────

@app.on_event("startup")
async def startup():
    _init_db()
    print(f"[Server] Datenbank: {DB_PATH.resolve()}")
    print(f"[Server] Max. Upload: {MAX_UPLOAD_MB} MB")


# ── Endpunkte ─────────────────────────────────────────────────────────────────

@app.get("/health")
async def health():
    job_count = len(_db_list())
    return {"status": "ok", "version": "0.2.0", "jobs": job_count}


@app.post("/upload", status_code=202)
async def upload_audio(
    background_tasks: BackgroundTasks,
    file: UploadFile = File(...),
):
    """Empfängt eine WAV-Datei per Streaming und startet die Pipeline."""
    if not file.filename or not file.filename.lower().endswith(".wav"):
        raise HTTPException(400, "Nur WAV-Dateien werden akzeptiert")

    job_id     = uuid.uuid4().hex[:8]
    audio_path = UPLOAD_DIR / f"{job_id}_{file.filename}"
    max_bytes  = MAX_UPLOAD_MB * 1024 * 1024

    # ── Streaming-Speicherung (kein read() in RAM) ────────────────────────────
    total_bytes = 0
    async with aiofiles.open(audio_path, "wb") as out:
        while chunk := await file.read(65_536):   # 64 KB Chunks
            total_bytes += len(chunk)
            if total_bytes > max_bytes:
                await out.close()
                audio_path.unlink(missing_ok=True)
                raise HTTPException(
                    413, f"Datei zu groß (max. {MAX_UPLOAD_MB} MB)"
                )
            await out.write(chunk)

    print(f"[Upload] {job_id} – {audio_path.name} ({total_bytes/1024:.1f} KB)")

    job = {
        "job_id":     job_id,
        "status":     "queued",
        "audio_file": str(audio_path),
        "result":     None,
        "error":      None,
    }
    _db_save(job)

    background_tasks.add_task(_run_job, job_id, audio_path)
    return {"job_id": job_id, "status": "queued"}


@app.get("/status/{job_id}")
async def job_status(job_id: str):
    job = _db_get(job_id)
    if not job:
        raise HTTPException(404, "Job nicht gefunden")
    return job


@app.get("/results/{job_id}")
async def get_result(job_id: str):
    job = _db_get(job_id)
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
    return _db_list()


@app.delete("/results/{job_id}", status_code=204)
async def delete_result(job_id: str):
    """Löscht Job-Eintrag sowie Audio- und Ergebnis-Datei."""
    job = _db_get(job_id)
    if not job:
        raise HTTPException(404, "Job nicht gefunden")
    # Dateien löschen
    if job.get("audio_file"):
        Path(job["audio_file"]).unlink(missing_ok=True)
    if job.get("result"):
        Path(job["result"]).unlink(missing_ok=True)
    with sqlite3.connect(DB_PATH) as conn:
        conn.execute("DELETE FROM jobs WHERE job_id = ?", (job_id,))


# ── Hintergrundverarbeitung ───────────────────────────────────────────────────

async def _run_job(job_id: str, audio_path: Path):
    _db_save({"job_id": job_id, "status": "queued",
              "audio_file": str(audio_path), "result": None, "error": None})

    def _status_cb(status: str, extra: str | None = None) -> None:
        update: dict = {
            "job_id":     job_id,
            "status":     status,
            "audio_file": str(audio_path),
            "result":     None,
            "error":      None,
        }
        # extra enthält bei "summarizing" den Transkript-Pfad
        if extra:
            update["transcript"] = extra
        _db_save(update)

    try:
        output_path = await asyncio.to_thread(
            run_pipeline,
            audio_path=audio_path,
            output_dir=OUTPUT_DIR,
            job_id=job_id,
            status_cb=_status_cb,
        )
        _db_save({"job_id": job_id, "status": "done",
                  "audio_file": str(audio_path),
                  "result": str(output_path), "error": None})
    except Exception as exc:
        _db_save({"job_id": job_id, "status": "failed",
                  "audio_file": str(audio_path),
                  "result": None, "error": str(exc)})
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
