# Pip

A desk companion with a face. Pico 2 W body, tiny_agent brain, rete_cpp
reflexes. Press its button and it winks before you let go — that reaction is
a Rete rule firing in microseconds, zero tokens. Hold the button and it
thinks — that one is an LLM agent choosing an expression, a chirp, and a
mood color.

Status: scaffold. Firmware and brain land next; the design spec lives in
[tiny_agent](https://github.com/tinyagent-cc/tiny_agent)
`docs/superpowers/specs/2026-08-22-pip-companion-design.md`.

## Hardware

| Part | Role |
|---|---|
| Raspberry Pi Pico 2 W | body: face, senses, sound |
| ILI9341 240x320 SPI | the face |
| VEML7700 | ambient light |
| MAX98357A I2S amp + speaker | chirps |
| Tactile button, RGB LED | interaction, mood |
| Raspberry Pi Zero 2 W | brain: tiny_agent + rete_cpp |
| Raspberry Pi 5 | llama-server |

`pip-lite`: the same firmware built for a bare Pico 2 W (internal temp
sensor + onboard LED), so the demo runs with no extra parts.

## Layout

    firmware/   Pico SDK C++ (body)
    brain/      tiny_agent + rete_cpp process (judgment + reflexes)

Wiring, build, and flash instructions arrive with the firmware.
