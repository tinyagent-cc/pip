"""Pip voice: Piper TTS on the Pi 5, behind a tiny HTTP API.

  GET  /health -> {"ok":true,"voice":"en_US-lessac-medium","sample_rate":16000}
  POST /tts    {"text":"..."} -> 200, application/octet-stream, raw little-endian
               s16 mono 16 kHz PCM, X-Sample-Rate: 16000, X-Duration-Ms: N
               400 on empty text, 413 beyond 300 characters.

Installed API, checked on the Pi 5 (piper-tts 1.7.0, python 3.13):

    PiperVoice.load(model_path, config_path=None, use_cuda=False,
                    espeak_data_dir=..., download_dir=None,
                    include_alignments=False) -> PiperVoice
    PiperVoice.synthesize(text, syn_config: SynthesisConfig | None = None,
                          include_alignments: bool = False) -> Iterable[AudioChunk]
    AudioChunk fields: sample_rate, sample_width, sample_channels,
                       audio_float_array, phonemes, phoneme_ids, ...
                       plus the audio_int16_array / audio_int16_bytes properties.
    SynthesisConfig fields: speaker_id, length_scale, noise_scale,
                            noise_w_scale, normalize_audio, volume.

The pet tweak: the voice's native 22050 Hz samples are resampled to 16 kHz as if
they had been recorded at `native_rate * PIP_VOICE_RATE`. At the 1.12 default
that plays 12 % faster and 12*log2(1.12) = 1.96 semitones higher, which is the
"+2 semitones, sounds like a small creature" the spec asks for.

Run: `~/pip-probe/venv/bin/python -m uvicorn voice:app --host 0.0.0.0 --port 8091`
"""

from __future__ import annotations

import asyncio
import contextlib
import json
import os
import pathlib
import threading
import time

import numpy as np
from fastapi import FastAPI, Request
from fastapi.responses import JSONResponse, Response

VOICE_PATH = os.path.expanduser(
    os.environ.get("PIP_PIPER_VOICE", "~/pip-probe/voices/en_US-lessac-medium.onnx")
)
VOICE_RATE = float(os.environ.get("PIP_VOICE_RATE", "1.12"))
LENGTH_SCALE = float(os.environ.get("PIP_PIPER_LENGTH_SCALE", "1.0"))
OUT_RATE = 16000
MAX_CHARS = 300

_voice = None
_voice_lock = threading.Lock()


@contextlib.asynccontextmanager
async def lifespan(_app):
    # Load the ONNX voice up front so the first /tts is as fast as the rest.
    # A failure here is not fatal: /health still answers and /tts reports 503.
    try:
        await asyncio.to_thread(get_voice)
    except Exception as exc:  # pragma: no cover - device-only path
        print(f"pip-voice: could not preload {VOICE_PATH}: {exc}", flush=True)
    yield


app = FastAPI(title="pip-voice", lifespan=lifespan)


def voice_name() -> str:
    return pathlib.Path(VOICE_PATH).stem


def _load_voice():
    """The seam the unit tests replace; the real one needs the Pi 5's piper."""
    from piper import PiperVoice  # noqa: PLC0415  (optional, device-only import)

    return PiperVoice.load(VOICE_PATH)


def get_voice():
    global _voice
    with _voice_lock:
        if _voice is None:
            _voice = _load_voice()
        return _voice


def _syn_config():
    if LENGTH_SCALE == 1.0:
        return None
    try:
        from piper import SynthesisConfig  # noqa: PLC0415
    except ImportError:
        return None
    return SynthesisConfig(length_scale=LENGTH_SCALE)


def _chunk_samples(chunk) -> np.ndarray:
    array = getattr(chunk, "audio_int16_array", None)
    if array is not None:
        return np.asarray(array, dtype=np.int16)
    return np.frombuffer(chunk.audio_int16_bytes, dtype=np.int16)


def resample(samples: np.ndarray, native_rate: int) -> np.ndarray:
    """Linear-interpolation resample to OUT_RATE, reading the input as if it had
    been captured at `native_rate * VOICE_RATE` (the pitch/speed tweak)."""
    if samples.size == 0:
        return samples.astype(np.int16)
    source_rate = native_rate * VOICE_RATE
    out_n = int(round(samples.size * OUT_RATE / source_rate))
    if out_n < 1:
        return np.zeros(0, dtype=np.int16)
    positions = np.arange(out_n, dtype=np.float64) * source_rate / OUT_RATE
    out = np.interp(positions, np.arange(samples.size, dtype=np.float64), samples)
    return np.clip(np.round(out), -32768, 32767).astype(np.int16)


def synthesize_pcm(text: str) -> bytes:
    """Piper -> raw little-endian s16 mono 16 kHz PCM."""
    chunks, native = [], None
    for chunk in get_voice().synthesize(text, _syn_config()):
        native = native or int(getattr(chunk, "sample_rate", 22050))
        chunks.append(_chunk_samples(chunk))
    if not chunks:
        return b""
    samples = np.concatenate(chunks) if len(chunks) > 1 else chunks[0]
    return resample(samples, native or 22050).astype("<i2").tobytes()


@app.post("/tts")
async def tts(request: Request):
    raw = await request.body()
    try:
        payload = json.loads(raw) if raw else {}
    except (ValueError, TypeError):
        payload = {}
    if not isinstance(payload, dict):
        payload = {}
    text = str(payload.get("text") or "").strip()

    if not text:
        return JSONResponse(status_code=400, content={"error": "text is required"})
    if len(text) > MAX_CHARS:
        return JSONResponse(
            status_code=413,
            content={"error": f"text longer than {MAX_CHARS} characters"},
        )

    started = time.monotonic()
    try:
        pcm = synthesize_pcm(text)
    except Exception as exc:  # a broken voice file, a piper failure
        return JSONResponse(status_code=503, content={"error": f"tts failed: {exc}"})
    duration_ms = int(round(len(pcm) / 2 / OUT_RATE * 1000))
    return Response(
        content=pcm,
        media_type="application/octet-stream",
        headers={
            "X-Sample-Rate": str(OUT_RATE),
            "X-Duration-Ms": str(duration_ms),
            "X-Synth-Ms": str(int(round((time.monotonic() - started) * 1000))),
        },
    )


@app.get("/health")
async def health():
    return {"ok": True, "voice": voice_name(), "sample_rate": OUT_RATE}
