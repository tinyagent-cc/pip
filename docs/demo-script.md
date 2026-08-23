# Pip: the demo script

A two to three minute film, shot in one sitting. Every scene captions itself
on the HUD, so there is no voice-over to record and nothing to edit in
afterwards. Read this page once, check the four health lines, then shoot.

Numbers marked `{{...}}` land from the acceptance run.

## Before the camera goes on

Four boxes, four commands. Any of them failing is a shot you will lose, so
run all four first.

| Part | Check | Wanted |
|---|---|---|
| body (Pico) | `curl -s http://<pip-ip>/senses` | `light_lux` a real number, `link.rx_frames` climbing between two calls |
| brain (Zero) | `curl -s http://<brain>:8080/health` | `"link":true,"cortex":true,"voice":true`, `"scene":""` |
| cortex (Jetson) | `curl -s http://orin-desktop.local:8090/health` | `whisper`, `vlm`, `camera`, `mic` all true |
| voice (Pi 5) | `curl -s http://pi5.local:8091/health` | `"ok":true` and the voice name |

Then the room: the desk light on and reachable, because the `night` shot
needs you to cover the sensor and uncover it again. The Brio wants something
worth describing in front of it. Frame the panel so the bottom 40 rows are in
shot; the HUD is where the film explains itself.

Leave a terminal open on `curl -s 'http://<brain>:8080/log?n=30'`. After the
first two shots it prints the reflex entries and the LLM entry in the same
`micros` column, which is the argument the whole film is making.

## The shots

Filmed in this order, which is not the order the scenes are numbered in. The
fallback shot only means something once the audience has watched a normal
answer arrive, so it comes late, and the tour closes.

### 1. reflex, the press

```
curl -X POST http://<brain>:8080/scene -d '{"name":"reflex"}'
```

Pip says **"Press my button."** Press it. The wink is already on the panel
before your finger is off the button. If you wait more than six seconds the
scene stages the press itself, which is how the same script runs unattended.

HUD: caption `reflex`, `rfx {{wire_reflex_us}}`, glyphs `W F B C` green.
About 15 seconds of footage.

Say to camera, if you are talking over it: that was a rule, not a model.

### 2. reflex, the hold

The same scene continues on its own. Pip says **"That was a rule.
Microseconds. Now hold me and ask something."**

Hold the button for a second and a half, then ask a question out loud. The
face goes to listening, then thinking, then the answer arrives spoken with
the same text in the bubble.

HUD: `jdg 9.7s`, `mind` reads `J`. Measured 2026-08-23: 9.8 s from release to
spoken answer, `judge_ms` 9758. Four of those seconds are the microphone
window (`--listen-seconds 4`), so the wait is honest rather than slow.

About 20 seconds of footage. The scene ends by itself once the answer lands.

### 3. night

```
curl -X POST http://<brain>:8080/scene -d '{"name":"night"}'
```

Pip says **"Cover my light sensor."** Cover it with a hand. The face goes
sleepy, a moon appears next to the lux bar, and the bar collapses. Pip then
says **"Dark means sleepy, and my rules cap the LED"**, asks for a bright red
LED, and reports what it actually got: **"Rule capped the LED to 40."**

That is the guardrail on camera: the agent asked for 255, the rule gave it 40,
and the LED next to the panel is dim rather than dark. Uncover the sensor
after five seconds and the scene brings the lights back itself.

HUD: caption `night`, moon lit, lux bar near zero. About 25 seconds.

### 4. fever

```
curl -X POST http://<brain>:8080/scene -d '{"name":"fever"}'
```

Pip says **"Warm my chip."** A thumb on the RP2350 for a few seconds is
usually enough to cross 35 C (`--hot-c`). Alert face, amber background, red
LED. If the chip stays cool the scene stages `temp.hot` after ten seconds.

HUD: caption `fever`, the temperature reading climbing past 35C. About 15
seconds, and no model is involved in any of it.

### 5. who

```
curl -X POST http://<brain>:8080/scene -d '{"name":"who"}'
```

Pip says **"Who's there?"**, takes a frame off the Brio, and says one sentence
about what is in front of it. On 2026-08-23 that was *"The desk in the room
has a clock on the wall."*

`/see` measured 2.6 s end to end, about 1.0 s once the VLM is warm, so run the
scene once before the take to pay the warm-up off camera.

HUD: caption `who`. About 10 seconds.

### 6. fallback

```
curl -X POST http://<brain>:8080/scene -d '{"name":"fallback"}'
```

Pip says **"Cortex offline. The next answer comes from the Pi five."** Then
**"Hold me and ask something."** Hold, ask, and watch the `C` glyph go grey
and `mind` change to `5`. The scene sets the flag in software and clears it
when it ends, so nothing has to be powered down between takes.

For the harder version, stop the Jetson's text model for real:

```
ssh orin@orin-desktop.local 'systemctl --user stop llama-text.service'
# shoot the take
ssh orin@orin-desktop.local 'systemctl --user start llama-text.service'
```

The spec budgets 40 s for a fallback answer against the Jetson's 10 s. The Pi
5 path has not been timed end to end yet: `{{fallback_judge_s}}`. Shoot this
one with room to run long. About 40 seconds.

### 7. tour

```
curl -X POST http://<brain>:8080/scene -d '{"name":"tour"}'
```

Two minutes, nobody at the desk. Pip introduces the body, the brain, the
cortex and the fallback in one sentence each, then plays `reflex`, `night` and
`who` with every wait staged. It ends on **"That's all of me. Press my button
any time."**

This is the shot to leave running while you tidy up, and it is what
`pip-brain --tour` boots into ten seconds after start. Three unattended runs
are one of the acceptance criteria, so run it three times and keep the `/log`
output.

## When something is wrong

| Symptom | What the HUD shows | One command |
|---|---|---|
| the wire is dead | `W` grey, events still work over WiFi, speech stops | `curl -s http://<pip-ip>/senses` twice: `link.rx_frames` has stopped climbing |
| the brain is down | `B` grey, the button does nothing | `curl -s http://<brain>:8080/health` |
| the cortex is down | `C` grey, no transcript, `look` answers "I can't see right now" | `curl -s http://orin-desktop.local:8090/health` |
| the voice is down | bubble only, a `drop` chirp instead of speech | `curl -s http://pi5.local:8091/health` |
| the Jetson model is down | `mind` reads `5` on an answer you expected `J` for | `curl -s http://orin-desktop.local:8081/health` |
| a scene returns 409 | the caption still shows the scene that has the stage | `curl -s http://<brain>:8080/health` and read `scene` |
| the panel is white | nothing, it never initialised | reboot the Pico: `picotool reboot -f` |

Two more worth knowing. `lux=-1.0` in `/senses` means the VEML7700 was not
found at boot and only a reboot re-probes it, so the `night` shot will not
work. And a scene that throws leaves a `note` line in `/log` with the
exception text, which is faster to read than the systemd journal:

```
curl -s 'http://<brain>:8080/log?n=50' | grep note
```

## After the shoot

Copy the numbers the film showed into the acceptance record: press to wink
from the `reflex` entries in `/log`, hold to answer from `judge_ms`, and the
three tour runs with their start and end times. Then re-render the frame
gallery so the README and tinyagent.cc show the same numbers the camera saw:

```
(cd build-tests && ./render_frames --scenes --reflex-us <measured> --judge-ms <measured>)
python3 scripts/frames-to-png.py build-tests/frames docs/frames
cp docs/frames/*.png ~/git/tiny_agent_cpp/site/assets/pip/
```
