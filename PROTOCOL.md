# Pip body protocol v1

Two transports carry the same commands. The **link** is a framed UART wire between the
body (Pico) and the brain (Pi Zero); **HTTP** is plain JSON on the LAN for config, debug
and as the fallback for events. No TLS: this is a same-LAN demo protocol, documented as such.

Commands mean the same thing on either transport, because both go through the one
dispatcher in `core/src/protocol.cpp`.

## Body endpoints (HTTP)

```
POST /express  {"emotion": "idle|happy|sleepy|thinking|alert|wink|surprised|sad|listening|talking"} -> {"ok": true}
POST /chirp    {"name": "rise|trill|drop|purr"}                     -> {"ok": true}
POST /led      {"r": 0-255, "g": 0-255, "b": 0-255}                 -> {"ok": true}
POST /say      {"text": "..."}                                      -> {"ok": true}
POST /hud      {"reflex_us", "judge_ms", "brain", "cortex", "mind", "scene"}  -> {"ok": true}
POST /scene    {"name": "..."}                                      -> {"ok": true}
GET  /ping                                                          -> {"pong": true}
GET  /senses   -> {"light_lux": float, "temp_c": float, "button": "up|down",
                   "link": {"rx_frames": n, "rx_bad": n, "audio_dropped": n},
                   "audio": {"free": bytes, "playing": bool}}
```

`/say` needs a non-empty `text`; anything past 95 characters is cut, not refused. The
bubble shows two lines of up to 24 characters at font scale 2 and holds for 3 s.

`/hud` takes any subset of its fields; an omitted field keeps its last value on the strip.
`reflex_us` and `judge_ms` are integers >= 0, `brain` and `cortex` are booleans, `mind` is a
one-character string (`"J"` Jetson, `"5"` Pi 5, `"-"` none), `scene` is <= 15 characters and
shows as the strip's top-line caption.

`/scene` needs a `name` of <= 15 characters.

Unknown names, out-of-range numbers and missing fields: 400 with `{"error": "..."}`.
A known route with the wrong method: 405. An unknown route: 404.

## The link (UART0, 921600 8N1)

Pico GP0 (TX) to Zero GPIO15 (RXD), Pico GP1 (RX) to Zero GPIO14 (TXD), common ground.
The brain opens `/dev/ttyAMA0`.

Frame:

```
0xA5 | type u8 | len u16 LE | payload[len] | crc8
```

`crc8` covers `type`, both length bytes and the payload. It is the CRC-8 of the CRC
catalogue: polynomial 0x07, MSB first, init 0x00, no reflection, no final xor; the check
value over `"123456789"` is 0xF4. Payload is at most 512 bytes.

| type | meaning |
|---|---|
| 0x01 | JSON, UTF-8, the objects below |
| 0x02 | AUDIO, raw s16 mono 16 kHz (Plan B consumes it; v1 counts it and drops it) |

A receiver that sees an unknown type byte, a length over 512, or a bad CRC counts the frame
bad and hunts for the next 0xA5. The offending byte is itself re-examined as a possible
sync byte, so a truncated frame followed immediately by a good one costs one bad count and
no good frames. Counters surface in `/senses` as `link.rx_frames`, `link.rx_bad`,
`link.audio_dropped`.

**Body to brain (up):**

```json
{"hello": {"fw": "v1", "protocol": 1}}      at boot
{"event": "button.press"}                   as they happen
{"senses": { ... the /senses object ... }}  every 500 ms
{"pong": true}                              in reply to a ping
```

**Brain to body (down):** one object per frame, `cmd` naming the command and the rest of
the object carrying its arguments, which are the same arguments as the HTTP body.

```json
{"cmd": "express", "emotion": "wink"}
{"cmd": "chirp",   "name": "rise"}
{"cmd": "led",     "r": 40, "g": 0, "b": 0}
{"cmd": "say",     "text": "hello from the wire"}
{"cmd": "hud",     "scene": "reflex", "reflex_us": 95, "judge_ms": 5800, "brain": true}
{"cmd": "scene",   "name": "night"}
{"cmd": "ping"}
```

The body answers a ping with a `{"pong":true}` frame. Other commands are not acknowledged
on the wire; a caller that wants a status code uses HTTP.

## Events (body -> brain)

```
{"event": "button.press"}
{"event": "button.hold"}      (fires once at 1.5s held)
{"event": "button.release"}
{"event": "light.low"}        (lux under threshold for 30s)
{"event": "light.high"}       (lux back over threshold)
```

An event goes over the link when the link is alive (a good frame arrived in the last 2 s),
otherwise as `POST <brain_url>/event`. Delivery is at-most-once; the body does not retry.
The brain treats events as facts, not commands.

## Brain endpoints

```
POST /event {"event": "..."}  -> 200 {"ok": true}
                                  400 {"ok": false, "error": "bad event"}  (not an object, or missing/unknown "event")
                              known events: button.press, button.hold, button.release,
                              light.low, light.high, temp.hot
GET  /health -> {"ok", "uptime_s", "events", "reflexes", "llm_calls", "night", "llm", "queue",
                 "link", "cortex", "voice", "mind", "judge_ms", "scene"}
              link: the UART is alive (a framed byte in the last 2 s)
              cortex/voice: last health check of the Jetson and the Pi 5 voice
              mind: who answered the last hold, "J" Jetson, "5" Pi 5, "-" nobody
              judge_ms: how long that answer took, -1 before the first one
              scene: the scene running now, "" when none
GET  /log?n= -> [{"t", "kind", "name", "detail", "micros"?, "prompt_tokens"?, "completion_tokens"?}, ...]
              (n defaults to 50; kind is one of event|reflex|llm|tool|note)
GET  /scenes -> ["reflex", "night", "fallback", "fever", "who", "tour"]
POST /scene {"name": "..."}   -> 200 {"ok": true, "scene": "..."}
                                  404 {"ok": false, "error": "no such scene"}
                                  409 {"ok": false, "error": "scene already running", "current": "..."}
```

Same plain-HTTP, no-TLS, same-LAN contract as the body.

### The link (brain <-> body over UART)

Frame: `0xA5 | type u8 | len u16 LE | payload | crc8(type, len_lo, len_hi, payload)`.
Type 1 is JSON, type 2 is audio (s16le mono 16 kHz, at most 512 bytes of
payload, i.e. 256 samples). CRC-8 poly 0x07, init 0, MSB first; the check
value both sides assert is `crc8("123456789") == 0xF4`. Anything that fails
to frame is dropped and the decoder resyncs on the next `0xA5`.

Up-frames (body -> brain):

    {"event": "button.press"}                                   same names as POST /event
    {"senses": {"light_lux": f, "temp_c": f, "button": "up|down"}}
    {"hello": {"fw": "v1", "protocol": 1}}
    {"pong": true}

Down-frames (brain -> body):

    {"cmd": "express", "emotion": "idle|happy|sleepy|thinking|alert|wink|surprised|sad|listening|talking"}
    {"cmd": "chirp", "name": "rise|trill|drop|purr|boot|sad"}
    {"cmd": "led", "r": 0-255, "g": 0-255, "b": 0-255}
    {"cmd": "say", "text": "..."}                               95 characters, cut by the brain
    {"cmd": "hud", ...}                                         any of reflex_us, judge_ms,
                                                                brain, cortex, mind, scene;
                                                                absent fields keep their captions
    {"cmd": "scene", "name": "..."}                             "" clears
    {"cmd": "ping"}

Audio frames are written paced to real time, at most 200 ms ahead of the
clock, and command frames interleave between them: a chirp during a spoken
sentence does not wait for the sentence to end.

## Versioning

Every response the body sends carries `X-Pip-Protocol: 1`, errors included. The link's
`hello` carries the same number. Breaking changes bump the header, the `hello` and the
version in this file together.

Changes from v0: four emotions (`surprised`, `sad`, `listening`, `talking`), the `/say`,
`/hud`, `/scene` and `/ping` routes, the link counters and audio block in `/senses`, and
the UART link itself.
