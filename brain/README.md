# pip-brain

The brain sits between the Pico body, a Jetson that listens and sees, and an
LLM. Reflex rules (rete_cpp) answer most events in microseconds, no tokens
spent; a tiny_agent judgment loop only wakes when you hold the button, and
that one takes seconds because it's a model call. `/log` shows both columns
side by side, which is the whole point of splitting the two.

Three boxes and a wire:

| Part | Hardware | Job |
|---|---|---|
| body | Pico W | face, chirps, LED, sensors, button, speaker |
| brain | Pi Zero 2 W | rules, the agent loop, the director, this binary |
| cortex | Jetson Orin | `/listen` (whisper), `/see` (a VLM), and the primary model |
| voice | Pi 5 | `/tts` (Piper), and the fallback model |

## Run locally

```
build-brain/pip-brain --link /dev/ttyAMA0 --pip http://192.168.1.110 \
  --llm http://orin-desktop.local:8081 --llm2 http://pi5.local:8081 \
  --cortex http://orin-desktop.local:8090 --voice http://pi5.local:8091
```

One of `--link` or `--pip` is required. With both, the UART carries events,
commands and speech, and the HTTP body is what the brain falls back to when
the link goes quiet for two seconds. Everything else is optional: no
`--cortex` and Pip stops listening and looking, no `--voice` and it only
shows the bubble, no `--llm` and there is no judgment layer at all.

## The wire

`0xA5 | type | len u16 LE | payload | crc8`, JSON one way or the other and
s16le 16 kHz audio going down. `PROTOCOL.md` has the frame table and every
command. To poke the body by hand, with `pip-brain` stopped so the two
readers don't split the bytes between them:

```
brain/scripts/link-probe.py /dev/ttyAMA0 '{"cmd":"express","emotion":"happy"}'
```

It sends one frame and prints every frame that comes back for two seconds.

## The hold flow

Hold the button for 1.5 s and the body sends `button.hold`. Then:

1. the `hold-listen` rule fires: `listening` face, `rise` chirp, microseconds;
2. the brain asks the cortex to listen for `--listen-seconds` (4 by default)
   and gets a transcript back;
3. `thinking` face, and the agent runs with the transcript and six tools:
   `express`, `chirp`, `led`, `senses`, `say`, `look`;
4. every `say` and the closing sentence go to the voice and stream to the
   body as audio while the bubble shows the text;
5. the HUD gets `judge_ms` and `mind`.

`mind` is who answered: `J` the Jetson, `5` the Pi 5, `-` nobody. It comes
from a marker middleware sitting inside `model_fallback`, so it reports the
model that actually replied rather than the one that was asked first.

If the cortex is down, the agent runs on the old no-transcript prompt and
`look` answers "I can't see right now". If the voice is down, the bubble
still shows and a `drop` chirp marks the missing speech. If both models are
down, the answer is empty, `mind` is `-`, and Pip chirps `drop`.

## Scenes

Six scripts, each on its own thread, one at a time:

| Scene | What it shows |
|---|---|
| `reflex` | press to wink in microseconds, then a hold and the seconds it costs |
| `night` | cover the sensor, then watch the guardrail cap a 255 LED to 40 |
| `fallback` | the next answer comes from the Pi 5, `mind: 5` on the HUD |
| `fever` | a hot chip, red LED, alert face |
| `who` | Pip looks through the camera and says what is in front of it |
| `tour` | about two minutes, unattended: every part introduced, then reflex, night and who with staged events |

```
curl http://<brain>:8080/scenes
curl -X POST http://<brain>:8080/scene -d '{"name":"reflex"}'
```

404 for a name that isn't a scene, 409 with the current scene's name when
one is already running. `--tour` runs the tour once, ten seconds after
start, which is what the unattended demo boots into.

Scenes wait for the real thing before staging one: `reflex` asks you to
press the button and only injects a press if six seconds pass with nobody
touching it. That is what lets the same script run with a person at the desk
or with nobody there.

## Endpoints

```
POST /event {"event": "..."}   -> 200 {"ok":true}  or  400 {"ok":false,"error":"bad event"}
GET  /health                   -> see below
GET  /log?n=50                 -> [{"t","kind","name","detail","micros"?,"prompt_tokens"?,"completion_tokens"?}, ...]
GET  /scenes                   -> ["reflex","night","fallback","fever","who","tour"]
POST /scene {"name":"..."}     -> 200 / 404 / 409
```

`/health` carries `ok`, `uptime_s`, `events`, `reflexes`, `llm_calls`,
`night`, `llm`, `queue`, plus v1's `link` (the UART is alive), `cortex` and
`voice` (last health check of each), `mind` and `judge_ms` (the last
answer), and `scene` (the scene running now, `""` when none).

`kind` on `/log` is one of `event`, `reflex`, `llm`, `tool`, `note`.
`micros` is a reflex's wall time, or an LLM call's wall time in microseconds
(so the two kinds sit in the same column and stay comparable).
`prompt_tokens` and `completion_tokens` only appear on `llm` entries. `n`
defaults to 50 if omitted or unparsable.

## Flags

| Flag | Default | Meaning |
|---|---|---|
| `--link DEV` | (none) | UART to the body, e.g. `/dev/ttyAMA0` |
| `--baud N` | 921600 | link speed |
| `--pip URL` | (none) | body over HTTP; with `--link` it becomes the fallback |
| `--listen-port N` | 8080 | brain's own HTTP port |
| `--llm URL` | (empty) | judgment LLM base URL; empty disables judgment |
| `--model NAME` | Qwen2.5-3B-Instruct | model name for `--llm` and `--llm2` |
| `--llm2 URL` | (empty) | fallback OpenAI-compatible server |
| `--cortex URL` | (empty) | Jetson ears and eyes; empty means no listening, no looking |
| `--voice URL` | (empty) | Pi 5 text to speech; empty means bubble only |
| `--tour` | off | run the tour once, ten seconds after start |
| `--listen-seconds N` | 4 | how long the cortex records on a hold |
| `--hot-c F` | 35 | temp.hot reflex threshold, Celsius |
| `--night-cap N` | 40 | max LED channel value while the room is dark |
| `--chirp-gap-ms N` | 5000 | minimum spacing between chirps |
| `--senses-poll-ms N` | 10000 | how often the brain polls senses and the service health |
| `--llm-timeout-s N` | 90 | read timeout per LLM round trip (no connect timeout is set) |
| `--help` | | print usage and exit |

Either `--link` or `--pip` must be given; the rest are optional.

## Host tests

On the Mac, point at local checkouts of `tiny_agent` and `rete_cpp` so the
configure step doesn't hit the network:

```
cmake -S brain -B build-brain -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_TOOLCHAIN_FILE=$HOME/.vcpkg/scripts/buildsystems/vcpkg.cmake \
  -DTINY_AGENT_DIR=$HOME/git/tiny_agent_cpp -DPIP_RETE_DIR=$HOME/git/rete_cpp
cmake --build build-brain -j
ctest --test-dir build-brain --output-on-failure
```

Without `TINY_AGENT_DIR`/`PIP_RETE_DIR`, CMake fetches the pinned commits
of both instead.

The suite runs against fakes for everything outside the process: `FakeLink`
(a socketpair speaking the wire), `FakePip` and `FakeBody`, `FakeCortex` and
`FakeVoice` (httplib servers), `FakeLlm` (a canned
`/v1/chat/completions`), and a fake clock that runs the two-minute tour in
milliseconds while still adding up its two minutes.

## Build on the Pi 5

The brain is aarch64/Debian 13 code; the binary that comes out runs
unchanged on the Pi Zero 2 W. Build where the CPU is fast, deploy where it
has to fit:

```
ssh pi@pi5.local 'cd ~/pip && git pull && brain/scripts/build-pi5.sh'
```

Reads `tiny_agent` from `~/tiny_agent_cpp` (override with `TINY_AGENT_DIR`)
and `rete_cpp` from its `build-reflex/_deps` if already fetched there
(override with `PIP_RETE_DIR`; if neither path exists, CMake fetches the pinned
rete_cpp itself).
Output lands in `brain/build-pi5/pip-brain`.

## Deploy to the Pi Zero

From the Mac, fetch the binary the Pi 5 just built, then push it to the Zero
and (re)start it as a systemd user service:

```
mkdir -p brain/build-pi5 && scp pi@pi5.local:~/pip/brain/build-pi5/pip-brain brain/build-pi5/
brain/scripts/deploy-zero.sh
```

Defaults: host `pi@pizerow.local`, `--link /dev/ttyAMA0`,
`--pip http://192.168.1.110`, `--llm http://orin-desktop.local:8081`,
`--llm2 http://pi5.local:8081`, `--cortex http://orin-desktop.local:8090`,
`--voice http://pi5.local:8091`, binary at `brain/build-pi5/pip-brain`. All
of them are overridable on the command line, and passing an empty string
drops that flag from the unit entirely (`--cortex ''` deploys a Pip that
does not listen).

The script generates the unit's `ExecStart` from those values and takes the
rest of `brain/deploy/pip-brain.service` verbatim, so change flags by
re-running the script, not by hand-editing the unit on the Zero. It enables
lingering so the service survives logout (falls back to a warning if that
needs sudo the script doesn't have), restarts the unit, and curls `/health`
on the Zero to confirm it's up.

## The Pico side

`firmware/config.h` needs `PIP_BRAIN_HOST` set to the Zero's IP and
`PIP_BRAIN_PORT` to 8080 (the brain's `--listen-port` default), then
rebuild and reflash:

```
picotool load -f -x build-fw/pip.uf2
```

## Demo script

| Action | Expected |
|---|---|
| press the button | wink, reflex logged in microseconds, `reflex_us` on the HUD |
| lights off 30 s | sleepy expression, LED capped from there on |
| lights back on | alert expression + trill chirp |
| hold the button, ask something | listening face, then thinking, then a spoken answer, `judge_ms` and `mind` on the HUD |
| `POST /scene {"name":"who"}` | Pip looks up and says what the camera sees |

`curl http://<brain>:8080/log?n=30` after any of the above shows the reflex
entries next to the LLM entry with its token counts, both in `micros`. The
rule evaluation itself is microseconds; a rule like `press-wink` also has to
push its command down the wire, so its logged `micros` is a little larger
than the rule match. The `bright-note` and `chirp-rate` lines don't touch
the body at all, so their `micros` is the honest rule-evaluation number.
That contrast is the whole demo.

## Same LAN, no auth

Plain HTTP, no TLS, no auth token, same as the body protocol, and the same
for the cortex and voice services. This is a same-LAN bench demo; the brain
trusts anything that can reach its port. Don't expose it past the LAN.
