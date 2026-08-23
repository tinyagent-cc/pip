"""Pip cortex: the Jetson's ears and eyes, behind a tiny HTTP API.

  GET  /health  -> {"ok":true,"whisper":bool,"vlm":bool,"camera":bool,"mic":bool}
  POST /listen  {"seconds":4}      -> {"text":"...","ms":N}   503 {"error":...}
  POST /see     {"question":"..."} -> {"text":"...","ms":N}   503 {"error":...}

`/listen` records from the Brio with `arecord` and transcribes with the CUDA
whisper.cpp build. `/see` grabs one MJPEG frame with `gst-launch-1.0` and asks
the local llama-server VLM about it. One `/listen` and one `/see` run at a time;
a second caller waits on an asyncio lock.

Silence is not an error: `/listen` returns `{"text":"","ms":N}` when whisper
heard nothing. 503 is reserved for a missing device, binary or VLM.

Run: `/usr/bin/python3 -m uvicorn cortex:app --host 0.0.0.0 --port 8090`
"""

from __future__ import annotations

import asyncio
import base64
import glob
import json
import os
import re
import shutil
import subprocess
import tempfile
import time

import httpx
from fastapi import FastAPI, Request
from fastapi.responses import JSONResponse


def _env_path(name: str, default: str) -> str:
    return os.path.expanduser(os.environ.get(name, default))


ALSA_DEVICE = os.environ.get("PIP_ALSA_DEVICE", "plughw:2,0")
WHISPER_BIN = _env_path("PIP_WHISPER_BIN", "~/tools/whisper.cpp/build/bin/whisper-cli")
WHISPER_MODEL = _env_path(
    "PIP_WHISPER_MODEL", "~/tools/whisper.cpp/models/ggml-base.en.bin"
)
ARECORD_BIN = os.environ.get("PIP_ARECORD_BIN", "arecord")
GST_BIN = os.environ.get("PIP_GST_BIN", "gst-launch-1.0")
CAMERA = os.environ.get("PIP_CAMERA", "/dev/video0")
FRAME_W = int(os.environ.get("PIP_FRAME_W", "1280"))
FRAME_H = int(os.environ.get("PIP_FRAME_H", "720"))
VLM_URL = os.environ.get("PIP_VLM_URL", "http://127.0.0.1:8082").rstrip("/")

MIN_SECONDS, MAX_SECONDS = 1, 10
MIN_FRAME_BYTES = 10_000
GST_TIMEOUT_S = 10.0
VLM_TIMEOUT_S = 60.0
HEALTH_TIMEOUT_S = 1.0

app = FastAPI(title="pip-cortex")

_listen_lock = asyncio.Lock()
_see_lock = asyncio.Lock()


# --- small helpers -----------------------------------------------------------


def _bin_ok(path: str) -> bool:
    """True when `path` resolves to an existing file, or to a program on PATH."""
    if os.sep in path:
        return os.path.isfile(path)
    return shutil.which(path) is not None


def _camera_present() -> bool:
    return os.path.exists(CAMERA)


def _http_client(timeout: float) -> httpx.Client:
    """The single seam for outbound HTTP. Tests replace this rather than
    patching `httpx.Client`, which Starlette's TestClient subclasses."""
    return httpx.Client(timeout=timeout)


def _fail(message: str) -> JSONResponse:
    return JSONResponse(status_code=503, content={"error": message})


def _ms(since: float) -> int:
    return int(round((time.monotonic() - since) * 1000))


async def _body(request: Request) -> dict:
    """Lenient body parse: the bench smokes curl without a JSON content type."""
    try:
        raw = await request.body()
    except Exception:
        return {}
    if not raw:
        return {}
    try:
        parsed = json.loads(raw)
    except (ValueError, TypeError):
        return {}
    return parsed if isinstance(parsed, dict) else {}


_NOISE = re.compile(r"\[[^\]]*\]|\([^)]*\)|\*[^*]*\*")


def clean_transcript(raw: str) -> str:
    """whisper-cli stdout -> a plain sentence. Bracketed noise markers
    ([BLANK_AUDIO], [ Silence ], (door creaks), *whispering*) are dropped."""
    if not raw:
        return ""
    return re.sub(r"\s+", " ", _NOISE.sub(" ", raw)).strip()


# --- /listen -----------------------------------------------------------------


def _record_and_transcribe(seconds: int) -> tuple[bool, str]:
    """(ok, text-or-error). Blocking; called from a worker thread."""
    fd, wav = tempfile.mkstemp(prefix="pip-listen-", suffix=".wav")
    os.close(fd)
    try:
        cmd = [
            ARECORD_BIN, "-D", ALSA_DEVICE, "-f", "S16_LE",
            "-r", "16000", "-c", "1", "-d", str(seconds), "-q", wav,
        ]
        try:
            rec = subprocess.run(
                cmd, capture_output=True, text=True, timeout=seconds + 15
            )
        except (subprocess.TimeoutExpired, OSError) as exc:
            return False, f"arecord failed: {exc}"
        if rec.returncode != 0:
            return False, f"arecord failed: {(rec.stderr or '').strip()[:200]}"

        cmd = [WHISPER_BIN, "-m", WHISPER_MODEL, "-f", wav, "-nt", "-np", "-l", "en"]
        try:
            out = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
        except (subprocess.TimeoutExpired, OSError) as exc:
            return False, f"whisper failed: {exc}"
        if out.returncode != 0:
            return False, f"whisper failed: {(out.stderr or '').strip()[:200]}"
        return True, clean_transcript(out.stdout or "")
    finally:
        try:
            os.unlink(wav)
        except OSError:
            pass


@app.post("/listen")
async def listen(request: Request):
    payload = await _body(request)
    try:
        seconds = int(payload.get("seconds", 4))
    except (TypeError, ValueError):
        seconds = 4
    seconds = max(MIN_SECONDS, min(MAX_SECONDS, seconds))

    if not _bin_ok(WHISPER_BIN):
        return _fail(f"whisper binary not found at {WHISPER_BIN}")
    if not _bin_ok(ARECORD_BIN):
        return _fail(f"arecord not found ({ARECORD_BIN}); no microphone")

    started = time.monotonic()
    async with _listen_lock:
        ok, result = await asyncio.to_thread(_record_and_transcribe, seconds)
    if not ok:
        return _fail(result)
    return {"text": result, "ms": _ms(started)}


# --- /see --------------------------------------------------------------------


def _grab_frame() -> tuple[bool, bytes | str]:
    """(ok, jpeg-bytes-or-error). Blocking; called from a worker thread."""
    workdir = tempfile.mkdtemp(prefix="pip-see-")
    try:
        single = os.path.join(workdir, "frame.jpg")
        cmd = [
            GST_BIN, "-q", "v4l2src", f"device={CAMERA}", "num-buffers=1", "!",
            f"image/jpeg,width={FRAME_W},height={FRAME_H}", "!",
            "filesink", f"location={single}",
        ]
        try:
            run = subprocess.run(
                cmd, capture_output=True, text=True, timeout=GST_TIMEOUT_S
            )
        except (subprocess.TimeoutExpired, OSError) as exc:
            return False, f"camera capture failed: {exc}"
        if run.returncode != 0:
            return False, f"camera capture failed: {(run.stderr or '').strip()[:200]}"

        data = _read_if_big_enough(single)
        if data is not None:
            return True, data

        # The Brio sometimes hands over a short first frame; take a few and
        # keep the last one.
        pattern = os.path.join(workdir, "frame-%d.jpg")
        cmd = [
            GST_BIN, "-q", "v4l2src", f"device={CAMERA}", "num-buffers=3", "!",
            f"image/jpeg,width={FRAME_W},height={FRAME_H}", "!",
            "multifilesink", f"location={pattern}",
        ]
        try:
            run = subprocess.run(
                cmd, capture_output=True, text=True, timeout=GST_TIMEOUT_S
            )
        except (subprocess.TimeoutExpired, OSError) as exc:
            return False, f"camera capture failed: {exc}"
        if run.returncode != 0:
            return False, f"camera capture failed: {(run.stderr or '').strip()[:200]}"

        frames = sorted(glob.glob(os.path.join(workdir, "frame-*.jpg")))
        for candidate in reversed(frames):
            data = _read_if_big_enough(candidate)
            if data is not None:
                return True, data
        return False, "camera capture failed: no usable frame"
    finally:
        shutil.rmtree(workdir, ignore_errors=True)


def _read_if_big_enough(path: str) -> bytes | None:
    try:
        if os.path.getsize(path) < MIN_FRAME_BYTES:
            return None
        with open(path, "rb") as fh:
            return fh.read()
    except OSError:
        return None


def _ask_vlm(question: str, jpeg: bytes) -> tuple[bool, str]:
    uri = "data:image/jpeg;base64," + base64.b64encode(jpeg).decode("ascii")
    payload = {
        "messages": [
            {
                "role": "user",
                "content": [
                    {"type": "text", "text": question},
                    {"type": "image_url", "image_url": {"url": uri}},
                ],
            }
        ],
        "max_tokens": 80,
        "temperature": 0.2,
    }
    try:
        with _http_client(VLM_TIMEOUT_S) as client:
            resp = client.post(f"{VLM_URL}/v1/chat/completions", json=payload)
    except httpx.HTTPError as exc:
        return False, f"vlm unreachable at {VLM_URL}: {exc}"
    if resp.status_code != 200:
        return False, f"vlm returned {resp.status_code}"
    try:
        text = resp.json()["choices"][0]["message"]["content"]
    except (ValueError, KeyError, IndexError, TypeError):
        return False, "vlm returned an unreadable body"
    return True, re.sub(r"\s+", " ", (text or "")).strip()


@app.post("/see")
async def see(request: Request):
    payload = await _body(request)
    question = payload.get("question") or "What do you see? One sentence."

    if not _camera_present():
        return _fail(f"camera not present at {CAMERA}")
    if not _bin_ok(GST_BIN):
        return _fail(f"{GST_BIN} not found; cannot grab a frame")

    started = time.monotonic()
    async with _see_lock:
        ok, frame = await asyncio.to_thread(_grab_frame)
        if not ok:
            return _fail(str(frame))
        ok, result = await asyncio.to_thread(_ask_vlm, str(question), frame)
    if not ok:
        return _fail(result)
    return {"text": result, "ms": _ms(started)}


# --- /health -----------------------------------------------------------------


def _mic_present() -> bool:
    try:
        out = subprocess.run(
            [ARECORD_BIN, "-l"], capture_output=True, text=True, timeout=5
        )
    except (subprocess.TimeoutExpired, OSError):
        return False
    return out.returncode == 0 and "card" in (out.stdout or "")


def _vlm_up() -> bool:
    try:
        with _http_client(HEALTH_TIMEOUT_S) as client:
            return client.get(f"{VLM_URL}/health").status_code == 200
    except httpx.HTTPError:
        return False


@app.get("/health")
async def health():
    whisper, vlm, camera, mic = await asyncio.gather(
        asyncio.to_thread(lambda: _bin_ok(WHISPER_BIN) and _bin_ok(WHISPER_MODEL)),
        asyncio.to_thread(_vlm_up),
        asyncio.to_thread(_camera_present),
        asyncio.to_thread(_mic_present),
    )
    # `ok` says the cortex process answered; the four flags say what it can do.
    return {"ok": True, "whisper": whisper, "vlm": vlm, "camera": camera, "mic": mic}
