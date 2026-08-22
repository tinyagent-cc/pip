# Pip

A desk companion with a face. Pico 2 W body, tiny_agent brain, rete_cpp
reflexes. Press its button and it winks before you let go: that reaction is
a Rete rule firing in microseconds, zero tokens. Hold the button and it
thinks; that one is an LLM agent choosing an expression, a chirp, and a
mood color.

Status: body firmware v0 on `main`. It builds warning-free in three configs
(full, `PIP_LITE`, and the I2S audio smoke) and flashes onto a Pico 2 W;
serial confirms the face loop runs, WiFi retries a failed join about every
10 s without stalling that loop, and the I2S tone loop runs on RP2350.
Chirps in the main firmware are still a print
stub, real audio is the next integration. Bench checks (the panel, the
button and LED, the light reading, the curl cookbook, the event POSTs, the
tone itself) are pending. The brain (tiny_agent + rete_cpp on a Pi Zero 2 W)
lands after it. Design spec: [tiny_agent](https://github.com/tinyagent-cc/tiny_agent)
`docs/superpowers/specs/2026-08-22-pip-companion-design.md`.

## Hardware and wiring

| Part | Role | Pico 2 W pins |
|---|---|---|
| ILI9341 240x320 SPI | the face | SCK GP18, MOSI GP19, CS GP17, DC GP20, RST GP21, LED 3V3, VCC 3V3, GND |
| VEML7700 | ambient light | SDA GP4, SCL GP5, VIN 3V3, GND |
| Tactile button | interaction | GP15 to GND (internal pull-up) |
| RGB LED (4-pin) | mood | R GP10, G GP11, B GP12 through 220 ohm, common to GND |
| MAX98357A + speaker | chirps (next plan) | BCLK GP26, LRC GP27, DIN GP28, VIN 3V3, GND |

`firmware/pins.hpp` is the source of truth for this table, and for the wiring diagram:

![Pip breadboard wiring](hardware/pip-wiring.png)

`hardware/pip-wiring.yml` is the [WireViz](https://github.com/wireviz/WireViz) source
(wire colours, resistor values, physical Pico pin numbers); `pip-wiring.svg` and
`pip-wiring.bom.tsv` are rendered from it. `scripts/check_wiring.py` runs in CI and fails
when the diagram and `pins.hpp` disagree. Regenerate after a pin change with
`pip install wireviz` (needs Graphviz) and `wireviz hardware/pip-wiring.yml`.
`PIP_LITE` needs no wiring: a bare Pico 2 W on USB.

If the eyes come up mirrored or upside down, that is MADCTL, not the wiring.
`firmware/src/drivers/ili9341.cpp` sends `0x36 0x28` (MV|BGR, landscape), which suits the
common red breakout and has not been checked on any other panel. Try `0xE8`, the other
landscape orientation, before pulling jumpers.

`pip-lite`: `-DPIP_LITE=ON` builds the same firmware for a bare Pico 2 W:
onboard LED answers `/led`, expressions and chirps print to serial, light
reads as -1.

## Build and flash

```
export PICO_SDK_PATH=~/git/pico-sdk PICO_TOOLCHAIN_PATH=<arm-gnu-toolchain>/bin
cp firmware/config.example.h firmware/config.h   # WiFi + brain address, never committed
cmake -S firmware -B build-fw -G Ninja && cmake --build build-fw
picotool load -x build-fw/pip.uf2                # board in BOOTSEL
picotool load -f -x build-fw/pip.uf2             # board already running Pip
```

Tested on the ARM GNU 14.2 toolchain with Pico SDK 2.1.1. Serial is USB CDC
(`/dev/cu.usbmodem*`, 115200). The face loop logs `pip: express <name>`,
`pip: chirp <name>`, `pip: led <r> <g> <b>` and `pip: event <name>`. WiFi is
non-blocking, so the eyes are on the panel before the radio is touched and
the loop never stalls on it:

    pip: wifi up ip=<addr>            link came up, HTTP server starts next
    pip: http server on :<port>
    pip: wifi down status=<n>         -3 is bad password, -2 no such SSID, -1 join failed
    pip: event dropped                a POST to the brain could not be queued

After a `wifi down` it retries about every 10 s, and reconnects on its own if
the AP comes back.

Host-side tests for the platform-free core (`core/`), 6 binaries, all green:
`cmake -S tests -B build-host && cmake --build build-host && ctest --test-dir
build-host`.

## Talk to the body

```
curl -s http://<pip-ip>/senses
# {"light_lux": <float>, "temp_c": <float>, "button": "up"|"down"}

curl -s -X POST http://<pip-ip>/express -d '{"emotion":"happy"}'
# {"ok": true}

curl -s -X POST http://<pip-ip>/chirp   -d '{"name":"trill"}'
# {"ok": true}

curl -s -X POST http://<pip-ip>/led     -d '{"r":0,"g":0,"b":60}'
# {"ok": true}
```

Events (`button.press`, `button.hold`, `button.release`, `light.low`,
`light.high`) are POSTed to `http://<brain>/event` as `{"event":"..."}`, at
most once each. The contract is `PROTOCOL.md`; none of this has run against
a live brain yet.

## Audio on RP2350

`pico_audio_i2s` builds and runs on RP2350 with BCLK GP26 / LRCLK GP27 / DIN
GP28 (tone heard: pending Riadh). Build it with `-DPIP_AUDIO_SMOKE=ON`
(needs `PICO_EXTRAS_PATH`); it links as a separate `pip_i2s_smoke` binary,
not yet wired into the main firmware's chirp path.

## Layout

    core/       platform-free C++17: face engine, HTTP + JSON, protocol, event FSMs (host-tested)
    firmware/   Pico SDK glue: drivers, WiFi, raw-tcp HTTP server, event POST, main loop
    tests/      host test harness for core/
