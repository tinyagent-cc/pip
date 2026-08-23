# Acceptance run, 2026-08-23

Bench run of the wow-demo spec against the real fleet, body on the wire. Firmware
from `0863eb6` (merge of fw-audio), brain from `c12a067` (wake ping), cortex and
voice services as merged in `64a8587`. Driven by `scripts/acceptance.sh --tours 3`
at 14:26 CEST, fallback timed separately at 14:33 and 14:34. Nobody was at the
desk: every press, hold and light change was staged by the director.

## Health before the run

| Check | Result |
|---|---|
| body `/senses` | `link.rx_frames 112, rx_bad 0, audio_dropped 0`, `audio.free 65534` |
| brain `/health` | `link true, cortex true, voice true, llm true` |
| cortex `/health` | `whisper true, vlm true, camera true, mic true` |
| voice `/health` | `en_US-lessac-medium, 16000 Hz` |

## Scenes, once each

| Scene | Start to end | What the log shows |
|---|---|---|
| reflex | 14:26:31 to 14:27:01 (30 s) | `press-wink` 165 µs, event 323 µs; hold: `hold-listen` 124 µs, two model turns (2.06 s + 1.15 s), `say "Sure, what's next?"`, `judge_ms` 3216, mind `J` |
| night | 14:27:01 to 14:27:15 (14 s) | `dark-sleepy` 133 µs with `night=on`; `bright-alert` 109 µs + `bright-note` 46 µs with `night=off` |
| fallback | 14:27:15 to 14:28:17 (62 s) | waited the full 60 s for a hold that nobody gave; re-run below with a staged hold |
| fever | 14:28:17 to 14:28:29 (12 s) | `hot-alert` 152 µs, `express=alert led=255,0,0` |
| who | 14:28:29 to 14:28:34 (5 s) | `/see` answered in 2.6 s: "A sitting room with a black recliner and a clock on the wall." |

Fallback, re-run with a hold injected 4 s into the scene:

| Run | hold to answer | `judge_ms` | first model turn | mind |
|---|---|---|---|---|
| 1 (Pi 5 cold on this prompt) | 49.8 s | 44268 | 36.8 s | `5` |
| 2 (prefix cached) | 24.5 s | 18986 | 19.0 s | `5` |

## Tour, three times unattended

| Run | Start | End | Length | Hold to answer | `judge_ms` | Notes |
|---|---|---|---|---|---|---|
| 1 | 14:28:34 | 14:29:52 | 78 s | 8.8 s | 3481 | press 114 µs, hold 126 µs, night 124/119 µs |
| 2 | 14:29:52 | 14:31:13 | 81 s | 8.9 s | 3257 | `services: cortex=down` for one probe while whisper held the mic, back up next probe |
| 3 | 14:31:13 | 14:32:29 | 76 s | 8.6 s | 3205 | press 111 µs, hold 134 µs |

No exception notes in `/log`. The brain stayed on the wire throughout (`link true`
on every health read, body `rx_bad 0`, `audio_dropped 0` at the end).

## Against the spec's success criteria

| Criterion | Result | Evidence |
|---|---|---|
| Press to wink is a rule, in microseconds, on the wire | pass | `press-wink` 105 to 165 µs (median 114), event 250 to 330 µs, `mind` untouched |
| Hold to spoken answer with the Jetson up, under 10 s | pass | 8.6 to 8.9 s to the answer text, about 9.0 s spoken; `judge_ms` 3.2 to 3.5 s |
| Speech plays on the body over the wire, mouth moves | pass (sample pipeline) | 108 frames per sentence, 0 dropped; `talking on/off` once per sentence on the Pico serial (fw-audio bench); nobody has listened with ears yet |
| Night: sleepy face, night flag, LED capped by the rule | pass | `dark-sleepy` + `night=on`; `night-led-cap` fired during the fallback hold |
| Fallback answer from the Pi 5 when the Jetson is skipped, under 40 s | open | 24.5 s warm, 49.8 s cold. The cold run breaks the 40 s budget; warm the Pi 5 with one hold before the take |
| Who: a sentence about the camera frame | pass | 2.6 s, plausible sentence |
| Fever: alert face, red LED | pass | `hot-alert` 152 µs |
| Tour runs three times unattended | pass | 78, 81, 76 s, no exceptions |
| HUD shows which layer answered and the numbers | pass by construction | `hud` frames sent after every reflex and judgment; panel not filmed this run |

## Open

- Fallback cold start is 49.8 s. Smallest fix: have the director's `fallback` scene
  send one throwaway prompt to the Pi 5 before it asks for the hold.
- `services: cortex=down` flickers for one probe while `/listen` holds the microphone
  (health probe timeout during whisper). Cosmetic; the glyph greys for 10 s.
- The speaker and the panel were verified through counters and the serial log, not
  by ear or camera. Riadh's first look covers that.

## Re-rendered after the run

`render_frames --scenes --reflex-us 114 --judge-ms 3216`, frames in `docs/frames/`
and on tinyagent.cc/pip.html.
