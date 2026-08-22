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

## Versioning

Every response the body sends carries `X-Pip-Protocol: 0`, errors included.
Breaking changes bump that header and the version in this file together.
