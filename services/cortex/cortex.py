"""Pip cortex: the Jetson's ears and eyes, behind a tiny HTTP API.

  GET  /health  -> {"ok":true,"whisper":bool,"vlm":bool,"camera":bool,"mic":bool,"search":bool}
  POST /listen  {"seconds":4}      -> {"text":"...","lang":"en","ms":N}   503 {"error":...}
  POST /see     {"question":"..."} -> {"text":"...","ms":N}   503 {"error":...}
  POST /search  {"query":"...","max_results":4}
                -> {"results":[{"title","snippet","url"}],"ms":N}   503 {"error":...}

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
# An English-only model (*.en.bin) cannot detect languages; the multilingual
# one runs with -l auto and reports what it heard.
WHISPER_MULTILINGUAL = not WHISPER_MODEL.endswith(".en.bin")
# Extra whisper-cli flags, e.g. "-ng" to skip the CUDA context: each spawn pays
# ~0.5 GB for one, and on a box already running two llama-servers that is the
# difference between a transcript and an OOM.
WHISPER_ARGS = os.environ.get("PIP_WHISPER_ARGS", "").split()
# A whisper-server on another board (e.g. http://pi5.local:8092). The mic stays
# here; only the wav travels. Set, it replaces the local whisper-cli spawn, which
# remains the fallback when unset.
WHISPER_URL = os.environ.get("PIP_WHISPER_URL", "")
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
_search_lock = asyncio.Lock()


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
    patching `httpx.Client`, which Starlette's TestClient subclasses.
    local_address pins the socket to IPv4: avahi sometimes returns an AAAA
    for .local names here, and the v6 route blackholes -- ~30 s of SYN
    retries before the fallback, which read as a hung /listen."""
    return httpx.Client(
        timeout=timeout,
        transport=httpx.HTTPTransport(local_address="0.0.0.0"),
    )


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


_LANG = re.compile(r"auto-detected language:\s*([a-z]{2})")


def parse_lang(stderr: str) -> str:
    """whisper-cli's stderr line 'auto-detected language: fr (p = ...)'."""
    m = _LANG.search(stderr or "")
    return m.group(1) if m else ""


def _transcribe_remote(wav: str) -> tuple[bool, str | tuple[str, str]]:
    """POST the wav to a whisper-server; verbose_json carries the detected
    language ('en' or 'english' -- the first two letters are the code either way
    for the languages Pip speaks)."""
    try:
        with open(wav, "rb") as fh, _http_client(60.0) as cli:
            r = cli.post(
                WHISPER_URL.rstrip("/") + "/inference",
                files={"file": ("audio.wav", fh, "audio/wav")},
                data={
                    "response_format": "verbose_json",
                    "language": "auto" if WHISPER_MULTILINGUAL else "en",
                },
            )
    except Exception as exc:
        return False, f"whisper server unreachable: {exc}"
    if r.status_code == 500 and "construction from null" in (r.text or ""):
        # whisper-server with VAD: a clip with no speech in it strips to nothing
        # and the reply is built from a null language. That is a verdict, not an
        # error -- nobody said anything.
        return True, ("", "en")
    if r.status_code != 200:
        return False, f"whisper server: HTTP {r.status_code} {r.text[:120]}"
    try:
        d = r.json()
    except ValueError:
        return False, "whisper server: unparseable reply"
    lang = (d.get("language") or "en")[:2] if WHISPER_MULTILINGUAL else "en"
    return True, (clean_transcript(d.get("text") or ""), lang or "en")


def _record_and_transcribe(seconds: int) -> tuple[bool, str | tuple[str, str]]:
    """(ok, (text, lang)-or-error). Blocking; called from a worker thread."""
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

        if WHISPER_URL:
            return _transcribe_remote(wav)
        lang_args = ["-l", "auto"] if WHISPER_MULTILINGUAL else ["-l", "en"]
        cmd = [WHISPER_BIN, "-m", WHISPER_MODEL, "-f", wav, "-nt", *lang_args, *WHISPER_ARGS]
        try:
            out = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
        except (subprocess.TimeoutExpired, OSError) as exc:
            return False, f"whisper failed: {exc}"
        if out.returncode != 0:
            return False, f"whisper failed: {(out.stderr or '').strip()[:200]}"
        lang = parse_lang(out.stderr) if WHISPER_MULTILINGUAL else "en"
        return True, (clean_transcript(out.stdout or ""), lang or "en")
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

    if not WHISPER_URL and not _bin_ok(WHISPER_BIN):
        return _fail(f"whisper binary not found at {WHISPER_BIN}")
    if not _bin_ok(ARECORD_BIN):
        return _fail(f"arecord not found ({ARECORD_BIN}); no microphone")

    started = time.monotonic()
    async with _listen_lock:
        ok, result = await asyncio.to_thread(_record_and_transcribe, seconds)
    if not ok:
        return _fail(result)
    text, lang = result
    return {"text": text, "lang": lang, "ms": _ms(started)}


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


# --- /search -----------------------------------------------------------------


SEARCH_TIMEOUT_S = 12
MAX_SEARCH_RESULTS = 8


def _search_available() -> bool:
    try:
        import ddgs  # noqa: F401, PLC0415

        return True
    except ImportError:
        return False


def _run_search(query: str, max_results: int) -> tuple[bool, list | str]:
    """(ok, results-or-error). Blocking; called from a worker thread."""
    try:
        from ddgs import DDGS  # noqa: PLC0415
    except ImportError:
        return False, "ddgs is not installed on the cortex"
    try:
        with DDGS(timeout=SEARCH_TIMEOUT_S) as client:
            hits = list(client.text(query, max_results=max_results))
    except Exception as exc:  # the library raises its own exception types
        return False, f"search failed: {exc}"
    results = [
        {
            "title": (h.get("title") or "").strip(),
            "snippet": (h.get("body") or "").strip(),
            "url": (h.get("href") or "").strip(),
        }
        for h in hits
        if isinstance(h, dict)
    ]
    return True, results


@app.post("/search")
async def search(request: Request):
    payload = await _body(request)
    query = str(payload.get("query") or "").strip()
    if not query:
        return JSONResponse(status_code=400, content={"error": "empty query"})
    try:
        max_results = int(payload.get("max_results", 4))
    except (TypeError, ValueError):
        max_results = 4
    max_results = max(1, min(MAX_SEARCH_RESULTS, max_results))

    started = time.monotonic()
    async with _search_lock:
        ok, results = await asyncio.to_thread(_run_search, query, max_results)
    if not ok:
        return _fail(str(results))
    return {"results": results, "ms": _ms(started)}


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
    whisper, vlm, camera, mic, searchable = await asyncio.gather(
        asyncio.to_thread(
            lambda: bool(WHISPER_URL) or (_bin_ok(WHISPER_BIN) and _bin_ok(WHISPER_MODEL))
        ),
        asyncio.to_thread(_vlm_up),
        asyncio.to_thread(_camera_present),
        asyncio.to_thread(_mic_present),
        asyncio.to_thread(_search_available),
    )
    # `ok` says the cortex process answered; the flags say what it can do.
    return {
        "ok": True, "whisper": whisper, "vlm": vlm,
        "camera": camera, "mic": mic, "search": searchable,
    }
