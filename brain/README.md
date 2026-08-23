# pip-brain

The brain sits between the Pico body and an LLM. Reflex rules (rete_cpp)
answer most events in microseconds, no tokens spent; a tiny_agent judgment
loop only runs when a reflex says the event needs a real decision, and that
one takes seconds because it's a model call. `/log` shows both columns side
by side, which is the whole point of splitting the two.

## Run locally

Build first (see Host tests below), then run the binary it produces:

```
build-brain/pip-brain --pip http://192.168.1.110 --listen-port 8080 \
  --llm http://pi5.local:8081 --model Qwen2.5-3B-Instruct
```

`--pip` is required: the body's base URL. `--llm` empty (the default) runs
reflex-only, no judgment layer, no model calls.

## Endpoints

```
POST /event {"event": "..."}   -> 200 {"ok":true}  or  400 {"ok":false,"error":"bad event"}
GET  /health                   -> {"ok","uptime_s","events","reflexes","llm_calls","night","llm","queue"}
GET  /log?n=50                 -> [{"t","kind","name","detail","micros"?,"prompt_tokens"?,"completion_tokens"?}, ...]
```

`kind` is one of `event`, `reflex`, `llm`, `tool`, `note`. `micros` is a
reflex's wall time, or an LLM call's wall time in microseconds (so the two
kinds sit in the same column and stay comparable). `prompt_tokens` and
`completion_tokens` only appear on `llm` entries. `n` on `/log` defaults to
50 if omitted or unparsable.

## Flags

| Flag | Default | Meaning |
|---|---|---|
| `--pip URL` | (required) | body base URL |
| `--listen-port N` | 8080 | brain's own HTTP port |
| `--llm URL` | (empty) | judgment LLM base URL; empty disables judgment |
| `--model NAME` | Qwen2.5-3B-Instruct | model name for `--llm` |
| `--llm2 URL` | (empty) | optional fallback OpenAI-compatible server |
| `--hot-c F` | 35 | temp.hot reflex threshold, Celsius |
| `--night-cap N` | 40 | max LED channel value while the room is dark |
| `--chirp-gap-ms N` | 5000 | minimum spacing between chirps |
| `--senses-poll-ms N` | 10000 | how often the brain polls `/senses` on the body |
| `--llm-timeout-s N` | 90 | timeout for a judgment LLM call |
| `--help` | | print usage and exit |

## Host tests

```
cmake -S brain -B build-brain -DCMAKE_BUILD_TYPE=Debug -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake
cmake --build build-brain -j
ctest --test-dir build-brain --output-on-failure
```

## Build on the Pi 5

The brain is aarch64/Debian 13 code; the binary that comes out runs
unchanged on the Pi Zero 2 W. Build where the CPU is fast, deploy where it
has to fit:

```
ssh pi@pi5.local 'cd ~/pip && git pull && brain/scripts/build-pi5.sh'
```

Reads `tiny_agent` from `~/tiny_agent_cpp` (override with `TINY_AGENT_DIR`)
and `rete_cpp` from its `build-reflex/_deps` if already fetched there
(override with `PIP_RETE_DIR`; leave it unset to let CMake fetch it fresh).
Output lands in `brain/build-pi5/pip-brain`.

## Deploy to the Pi Zero

From the Mac, fetch the binary the Pi 5 just built, then push it to the Zero
and (re)start it as a systemd user service:

```
mkdir -p brain/build-pi5 && scp pi@pi5.local:~/pip/brain/build-pi5/pip-brain brain/build-pi5/
brain/scripts/deploy-zero.sh
```

Defaults: host `pi@pizerow.local`, `--pip http://192.168.1.110`,
`--llm http://pi5.local:8081`, binary at `brain/build-pi5/pip-brain`. All
four are overridable: `deploy-zero.sh --pip URL --llm URL --host user@host --bin path`.
It installs `brain/deploy/pip-brain.service` under
`~/.config/systemd/user/`, patches in the `--pip`/`--llm` values passed on
the command line, enables lingering so the service survives logout (falls
back to a warning if that needs sudo the script doesn't have), restarts the
unit, and curls `/health` on the Zero to confirm it's up.

The systemd unit itself (`brain/deploy/pip-brain.service`) ships with the
bench defaults baked in (`--pip http://192.168.1.110 --llm http://pi5.local:8081`);
`deploy-zero.sh` is what rewrites those two values on deploy, so edit flags
by re-running the script, not by hand-editing the unit on the Zero.

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
| press the button | wink, reflex fires in microseconds |
| lights off 30 s | sleepy expression |
| lights back on | alert expression + trill chirp |
| hold the button 1.5 s | thinking expression, then the model's reaction lands a few seconds later |

`curl http://<brain>:8080/log?n=30` after any of the above shows the reflex
entries in microseconds sitting next to the LLM entry in milliseconds with
its token counts. That contrast is the whole demo.

## Same LAN, no auth

Plain HTTP, no TLS, no auth token, same as the body protocol. This is a
same-LAN bench demo; the brain trusts anything that can reach its port.
Don't expose it past the LAN.
