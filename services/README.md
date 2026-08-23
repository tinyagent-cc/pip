# Pip services

Two small HTTP services the brain calls over the LAN. No auth, no secrets.

| Device | Service | Port | Unit | Runtime |
|---|---|---|---|---|
| Jetson Orin Nano (`orin@orin-desktop.local`) | text LLM | 8081 | `llama-text.service` | llama-server (CUDA) |
| Jetson | VLM (`/see`) | 8082 | `llama-vlm.service` | llama-server (CUDA) |
| Jetson | cortex (`/listen`, `/see`) | 8090 | `pip-cortex.service` | `/usr/bin/python3` + uvicorn |
| Pi 5 (`pi@pi5.local`) | voice (`/tts`) | 8091 | `pip-voice.service` | `~/pip-probe/venv` + uvicorn |

All three Jetson units and the Pi 5 unit are **systemd user units** (lingering is
enabled on both boxes); nothing needs sudo.

Never touch Pi 5 `:8080` (Riadh's own llama-server) or Pi 5 `:8081` (the brain's
fallback LLM). Jetson `:8091` is occupied by an unrelated uvicorn; the cortex
uses `:8090`.

---

## Jetson: model choice and measured footprint

Measured 2026-08-23 on JetPack R36.5, CUDA 12.6, 7607 MB of unified memory.

### Which VLM

`SmolVLM-500M-Instruct-Q8_0` + `mmproj-SmolVLM-500M-Instruct-f16`
(`ggml-org/SmolVLM-500M-Instruct-GGUF`, downloaded into `~/models/smolvlm/`).

Qwen2.5-VL-3B was **not** re-tested: the design spec already recorded it
returning no `tool_calls`, breaking image answers at 4k context and filling the
8 GB. SmolVLM answers sensibly in ~1 s, so there was no reason to spend the
memory. Sample answers on `~/pip-probe/frame.jpg`, question
"What is in this picture? One sentence.":

```
2419 ms | A man in a white shirt is pointing to a door in a house.
1000 ms | A man at a desk with a clock on the wall and a door next to him.
1018 ms | A man wearing glasses is pointing at a door with a script that says "think.do".
```

First call pays the ~1.4 s prompt/mmproj warm-up; steady state is ~1.0 s.

### Memory

`free -m` before the units started, and with both llama-servers loaded:

| | total | used | free | swap used |
|---|---|---|---|---|
| baseline (desktop + openclaw stack already running) | 7607 | 2255 | 4464 | 1746 |
| with `llama-text` + `llama-vlm` | 7607 | 5202 | 148 | 1737 |

Per-process RSS (`ps -o pid,rss,args -C llama-server`), unified memory so this
includes the GPU allocation:

| process | RSS |
|---|---|
| `llama-text` (Qwen2.5-3B-Instruct Q4_K_M, `-ngl 99 -c 4096`) | 2701 MB |
| `llama-vlm` (SmolVLM-500M Q8_0 + f16 mmproj, `-ngl 99 -c 2048`) | 1053 MB |
| **Pip trio total** (+ whisper-cli, transient, and the cortex uvicorn ~40 MB) | **≈ 3.8 GB** |

Budget was "text LLM + VLM + whisper + cortex < 6.5 GB resident": **met**, at
about 3.8 GB.

**Swap ruling.** The plan asked for "no swap in use". The Jetson had 1746 MB of
swap already in use at baseline — `gnome-software`, `node`, `qdrant`,
`update-manager`, an unrelated `uvicorn`, i.e. the desktop and the OpenClaw stack
that were running before this work started and that this plan must not stop.
Swap in use went *down* to 1737 MB after loading both models, so the Pip services
add no swap pressure of their own. Nothing else can be done without sudo (no
`swapoff`, no desktop teardown), so this is recorded rather than fixed.

### Latency

| call | measured |
|---|---|
| text LLM `/v1/chat/completions`, "Say hi in five words", 32 max tokens | 0.43 s cold, 0.22 s warm |
| VLM `/v1/chat/completions` with a 186 KB JPEG data URI, 80 max tokens | 2.4 s cold, ~1.0 s warm |

---

## Deploy

From the Mac, in the repo root:

```sh
services/scripts/deploy-jetson.sh   # rsync + install-jetson.sh + health
services/scripts/deploy-pi5.sh      # rsync + install-pi5.sh + health + a spoken smoke
```

Restart one thing:

```sh
ssh orin@orin-desktop.local 'systemctl --user restart pip-cortex.service'
ssh pi@pi5.local             'systemctl --user restart pip-voice.service'
```

Logs: `journalctl --user -u pip-cortex.service -f` (same for the others).
