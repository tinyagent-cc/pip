# Pip services

Two small HTTP services the brain calls over the LAN: the **cortex** on the
Jetson (ears and eyes) and the **voice** on the Pi 5 (mouth). No auth, no
secrets, no cloud.

| Device | Service | Port | Unit | Runtime |
|---|---|---|---|---|
| Jetson Orin Nano (`orin@orin-desktop.local`) | text LLM (judgment) | 8081 | `llama-text.service` | llama-server, CUDA |
| Jetson | VLM (backs `/see`) | 8082 | `llama-vlm.service` | llama-server, CUDA |
| Jetson | **cortex** `/listen` `/see` `/health` | 8090 | `pip-cortex.service` | `/usr/bin/python3` 3.10 + uvicorn |
| Pi 5 (`pi@pi5.local`) | **voice** `/tts` `/health` | 8091 | `pip-voice.service` | `~/pip-probe/venv` (py 3.13) + uvicorn |

All four are **systemd user units** (lingering is on for both boxes); nothing
here needs sudo, at install or at run time.

Ports that belong to someone else and must be left alone: Pi 5 `:8080`
(Riadh's own llama-server), Pi 5 `:8081` (the brain's fallback LLM), Jetson
`:8091` (an unrelated uvicorn that was already there).

---

## Contracts

```
GET  /health   -> {"ok":true,"whisper":bool,"vlm":bool,"camera":bool,"mic":bool}
POST /listen   {"seconds":4}      -> {"text":"...","ms":N}
POST /see      {"question":"..."} -> {"text":"...","ms":N}
                                     503 {"error":"..."} when a part is down
```

```
GET  /health   -> {"ok":true,"voice":"en_US-lessac-medium","sample_rate":16000}
POST /tts      {"text":"..."} -> 200 application/octet-stream
                                 body: raw little-endian s16 mono 16 kHz PCM
                                 X-Sample-Rate: 16000  X-Duration-Ms: N
                                 400 empty text, 413 over 300 chars
```

`ok` means "the service answered". On the cortex the four flags say what it can
actually do, so the brain can degrade one sense without treating the whole
cortex as dead. Silence is not an error: `/listen` returns `{"text":"","ms":N}`
when whisper heard nothing; 503 is reserved for a missing device, binary or VLM.

Both services parse the request body leniently (raw JSON, no `Content-Type`
required), so the bench `curl -d '{...}'` smokes work without extra flags.

`X-Synth-Ms` is an extra, non-contractual header on `/tts`: how long Piper took,
as against `X-Duration-Ms`, how long the audio plays.

---

## Curl smokes

```sh
curl -s http://orin-desktop.local:8090/health
curl -s -X POST http://orin-desktop.local:8090/listen -d '{"seconds":3}'
curl -s -X POST http://orin-desktop.local:8090/see -d '{"question":"What do you see? One sentence."}'

curl -s http://pi5.local:8091/health
curl -s -X POST http://pi5.local:8091/tts -d '{"text":"Hello, I am Pip."}' -o /tmp/pip.pcm
ffplay -f s16le -ar 16000 -ac 1 /tmp/pip.pcm    # or wrap into a WAV, see deploy-pi5.sh
```

Live outputs, 2026-08-23:

```
/health  {"ok":true,"whisper":true,"vlm":true,"camera":true,"mic":true}
/see     {"text":"The desk in the room has a clock on the wall.","ms":2639}
/listen  {"text":"","ms":4483}          <- empty room, nobody speaking: correct
/health  {"ok":true,"voice":"en_US-lessac-medium","sample_rate":16000}
/tts     19074 samples, 1.19 s of audio, X-Synth-Ms: 194
```

The `/tts` output was fed back through the Jetson's whisper, which transcribed
it as **"Hello, I am Pip."** — an end-to-end check that the PCM format, the
sample rate and the pitch tweak all survive.

---

## Deploy

From the repo root on the Mac:

```sh
services/scripts/deploy-jetson.sh   # rsync + install-jetson.sh + health
services/scripts/deploy-pi5.sh      # rsync + install-pi5.sh + health + spoken smoke
```

`install-jetson.sh` downloads the SmolVLM GGUF pair if it is missing, installs
the three units, `daemon-reload`, `enable --now`, and waits for each health
endpoint. `install-pi5.sh` checks the venv and the voice files first, then does
the same for `pip-voice`.

Restart or read one thing:

```sh
ssh orin@orin-desktop.local 'systemctl --user restart pip-cortex.service'
ssh orin@orin-desktop.local 'journalctl --user -u pip-cortex.service -n 50'
ssh pi@pi5.local            'systemctl --user restart pip-voice.service'
```

---

## Environment variables

### cortex (`pip-cortex.service`)

| Variable | Default | Note |
|---|---|---|
| `PIP_ALSA_DEVICE` | `plughw:2,0` | the Brio; `arecord -l` confirms card 2 is `BRIO [Logitech BRIO]` |
| `PIP_WHISPER_BIN` | `~/tools/whisper.cpp/build/bin/whisper-cli` | CUDA build |
| `PIP_WHISPER_MODEL` | `~/tools/whisper.cpp/models/ggml-base.en.bin` | |
| `PIP_ARECORD_BIN` | `arecord` | resolved on PATH |
| `PIP_GST_BIN` | `gst-launch-1.0` | resolved on PATH |
| `PIP_CAMERA` | `/dev/video0` | the Brio's MJPEG node |
| `PIP_FRAME_W` / `PIP_FRAME_H` | `1280` / `720` | |
| `PIP_VLM_URL` | `http://127.0.0.1:8082` | |

`/listen` clamps `seconds` to 1..10. One `/listen` and one `/see` run at a time
(asyncio locks); a second caller waits rather than fighting for the device.

### voice (`pip-voice.service`)

| Variable | Default | Note |
|---|---|---|
| `PIP_PIPER_VOICE` | `~/pip-probe/voices/en_US-lessac-medium.onnx` | `.onnx.json` must sit beside it |
| `PIP_VOICE_RATE` | `1.12` | the pet tweak, see below |
| `PIP_PIPER_LENGTH_SCALE` | `1.0` | Piper's own speed knob, applied before the resample |

### Swapping the Piper voice

Download an `.onnx` + `.onnx.json` pair from
`rhasspy/piper-voices` on Hugging Face into `~/pip-probe/voices/`, then point the
unit at it and restart:

```sh
ssh pi@pi5.local
  cd ~/pip-probe/voices
  ~/pip-probe/venv/bin/python -m piper.download_voices --download-dir ~/pip-probe/voices en_GB-alba-medium
  systemctl --user edit --full pip-voice.service   # change PIP_PIPER_VOICE
  systemctl --user restart pip-voice.service
```

`/health` reports the voice by its filename stem, so the brain and the HUD pick
up the new name with no code change. Anything other than a 22050 Hz voice still
works: the native rate is read off the Piper audio chunk, not assumed.

---

## The pet tweak

Piper renders `en_US-lessac-medium` at 22050 Hz. The service resamples to 16 kHz
with numpy linear interpolation, reading the samples **as if they had been
recorded at `native_rate * PIP_VOICE_RATE`**. At the 1.12 default that plays
12 % faster and `12*log2(1.12) = 1.96` semitones higher: small, quick, a bit of
a creature, still fully intelligible (whisper transcribed it back verbatim).

> **Ruling.** The plan's prose said "an effective source rate of
> `native_rate / PIP_VOICE_RATE`", but division lowers pitch and slows the
> speech, and it contradicts the same plan's own expected numbers (14286 samples
> and 893 ms for a 22050-sample chunk, which only come out of multiplication).
> The arithmetic and the stated intent both point at multiply, so the code
> multiplies. `PIP_VOICE_RATE=1.0` gives a plain, unmodified 16 kHz downsample.

---

## Jetson: model choice and measured footprint

Measured 2026-08-23 on JetPack R36.5, CUDA 12.6, 7607 MB of unified memory.

### Which VLM

`SmolVLM-500M-Instruct-Q8_0` + `mmproj-SmolVLM-500M-Instruct-f16`, from
`ggml-org/SmolVLM-500M-Instruct-GGUF`, in `~/models/smolvlm/`.

Qwen2.5-VL-3B was **not** re-tested. The design spec already recorded it
returning no `tool_calls`, breaking image answers at 4k context and filling the
8 GB, and SmolVLM answers sensibly in about a second, so there was no reason to
spend the memory on a second trial. Sample answers on `~/pip-probe/frame.jpg`,
question "What is in this picture? One sentence.":

```
2419 ms | A man in a white shirt is pointing to a door in a house.
1000 ms | A man at a desk with a clock on the wall and a door next to him.
1018 ms | A man wearing glasses is pointing at a door with a script that says "think.do".
```

The first call pays a ~1.4 s prompt and mmproj warm-up; steady state is ~1.0 s.

### Memory

`free -m`, before anything started and with all three Jetson units running:

| | total | used | free | swap used |
|---|---|---|---|---|
| baseline (desktop + OpenClaw stack, already running) | 7607 | 2255 | 4464 | 1746 |
| `llama-text` + `llama-vlm` | 7607 | 5202 | 148 | 1737 |
| + `pip-cortex`, after serving `/see` and `/listen` | 7607 | 6007 | 338 | 1736 |

Per-process RSS. This is unified memory, so the llama figures include the GPU
allocation:

| process | RSS |
|---|---|
| `llama-text` (Qwen2.5-3B-Instruct Q4_K_M, `-ngl 99 -c 4096 --jinja -t 4`) | 2717 MB |
| `llama-vlm` (SmolVLM-500M Q8_0 + f16 mmproj, `-ngl 99 -c 2048 -t 2`) | 1320 MB |
| `pip-cortex` uvicorn | 49 MB |
| `whisper-cli` | transient, only while `/listen` runs |
| **Pip total** | **≈ 4.09 GB** |

Budget was "text LLM + VLM + whisper + cortex under 6.5 GB resident":
**met at about 4.1 GB**, 2.4 GB of headroom.

> **Ruling on swap.** The plan also asked for "no swap in use". The Jetson had
> 1746 MB of swap already in use *before* any of this started —
> `gnome-software`, `node`, `qdrant`, `update-manager` and an unrelated
> `uvicorn`, i.e. the desktop session and the OpenClaw stack, none of which this
> work is allowed to stop. Swap in use went slightly *down* (1746 → 1736 MB) as
> the models loaded, so the Pip services add no swap pressure of their own.
> Without sudo there is no `swapoff` and no desktop teardown available, so this
> is recorded rather than fixed.

### Latency

| call | measured |
|---|---|
| text LLM `/v1/chat/completions`, "Say hi in five words", 32 max tokens | 0.43 s cold, 0.22 s warm |
| VLM `/v1/chat/completions`, 186 KB JPEG data URI, 80 max tokens | 2.4 s cold, ~1.0 s warm |
| cortex `/see` end to end (frame grab + VLM) | 2.6 s |
| cortex `/listen`, 3 s of audio (record + whisper base.en on CUDA) | 4.5 s, i.e. ~1.5 s of transcription |
| voice `/tts`, one short sentence | 194 ms synth cold (preloaded voice), 0.42-0.46 s round trip from the Mac |

Against the spec's "hold to spoken answer under 10 s": `/listen` 4.5 s +
judgment 0.2 s + `/tts` 0.5 s leaves comfortable room.

---

## Tests

Unit tests mock every subprocess and every HTTP call; they touch no hardware and
run on the Mac.

```sh
python3 -m venv ~/tools/pip-services-venv
~/tools/pip-services-venv/bin/pip install fastapi httpx pytest numpy uvicorn
~/tools/pip-services-venv/bin/python -m pytest services -q
```

> **Ruling on the mocking seam.** The plan said to patch `httpx.Client.post`.
> That cannot work: Starlette's `TestClient` *is* an `httpx.Client` subclass, so
> patching the class breaks the test client itself. The cortex opens outbound
> HTTP through one module-level `_http_client(timeout)` factory and the tests
> replace that instead. Same idea in the voice service, where `_load_voice()` is
> the seam that keeps Piper off the Mac.
