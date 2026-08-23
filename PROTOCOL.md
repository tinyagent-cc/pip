# Pip body protocol v0

Plain HTTP JSON on the LAN. The body (Pico) serves; the brain calls.
No TLS: this is a same-LAN demo protocol, documented as such.

## Body endpoints

POST /express  {"emotion": "idle|happy|sleepy|thinking|alert|wink"} -> {"ok": true}
POST /chirp    {"name": "rise|trill|drop|purr"}                     -> {"ok": true}
POST /led      {"r": 0-255, "g": 0-255, "b": 0-255}                 -> {"ok": true}
GET  /senses   -> {"light_lux": float, "temp_c": float, "button": "up|down"}

Unknown emotion/chirp names: 400 with {"error": "..."}.

## Events (body -> brain webhook)

POST <brain_url>/event with one of:
  {"event": "button.press"}
  {"event": "button.hold"}      (fires once at 1.5s held)
  {"event": "button.release"}
  {"event": "light.low"}        (lux under threshold for 30s)
  {"event": "light.high"}       (lux back over threshold)

Delivery is at-most-once; the body does not retry. The brain treats events
as facts, not commands.

## Brain endpoints

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

Every response the body sends carries `X-Pip-Protocol: 0`, errors included.
Breaking changes bump that header and the version in this file together.
