#!/usr/bin/env python3
"""Fail if hardware/pip-wiring.yml disagrees with firmware/pins.hpp.

No third-party imports: the YAML is read with regexes, so CI needs only python3.
Checks, for every GPIO constant in pins.hpp:
  1. the Pico connector labels that GPIO as "GP<n> <NAME>";
  2. that label sits on the right physical pin of the Pico 2 W header;
  3. the physical pin is used by at least one connection.
"""
import re, sys, pathlib

ROOT = pathlib.Path(__file__).resolve().parent.parent
PINS = ROOT / "firmware" / "pins.hpp"
YML = ROOT / "hardware" / "pip-wiring.yml"

# Pico / Pico 2 W header: GPIO -> physical pin
GPIO_TO_PIN = {0:1,1:2,2:4,3:5,4:6,5:7,6:9,7:10,8:11,9:12,10:14,11:15,12:16,13:17,14:19,15:20,
               16:21,17:22,18:24,19:25,20:26,21:27,22:29,26:31,27:32,28:34}

def pins_hpp():
    text = PINS.read_text()
    return {m.group(1): int(m.group(2)) for m in re.finditer(r"\b([A-Z][A-Z0-9_]*)\s*=\s*(\d+)", text)}

def pico_labels(text):
    block = re.search(r"^  Pico:\n(.*?)(?=^  [A-Za-z])", text, re.S | re.M).group(1)
    labels = re.search(r"pinlabels:\s*\[(.*?)\]", block, re.S).group(1)
    return [l.strip() for l in labels.replace("\n", " ").split(",")]

def pico_pins_used(text):
    used = set()
    for m in re.finditer(r"-\s*Pico:\s*\[([^\]]*)\]", text):
        used.update(int(p) for p in re.findall(r"\d+", m.group(1)))
    return used

def main():
    consts = pins_hpp()
    text = YML.read_text()
    labels = pico_labels(text)
    used = pico_pins_used(text)
    errors = []
    if len(labels) != 40:
        errors.append(f"Pico pinlabels has {len(labels)} entries, expected 40")
    for name, gpio in consts.items():
        want = f"GP{gpio} {name}"
        phys = GPIO_TO_PIN.get(gpio)
        if phys is None:
            errors.append(f"{name}: GP{gpio} is not a header GPIO on the Pico")
            continue
        have = labels[phys - 1] if phys - 1 < len(labels) else "<missing>"
        if have != want:
            errors.append(f"{name}: physical pin {phys} is labelled '{have}', pins.hpp says '{want}'")
        if phys not in used:
            errors.append(f"{name}: GP{gpio} (pin {phys}) is never wired in a connection")
    for label in labels:
        m = re.fullmatch(r"GP(\d+) ([A-Z][A-Z0-9_]*)", label)
        if not m:
            continue
        gpio, name = int(m.group(1)), m.group(2)
        if name not in consts:
            errors.append(f"label 'GP{gpio} {name}' has no constant in pins.hpp")
        elif consts[name] != gpio:
            errors.append(f"label 'GP{gpio} {name}' but pins.hpp has {name} = {consts[name]}")
    if errors:
        print("wiring drift:\n  " + "\n  ".join(errors)); sys.exit(1)
    print(f"wiring ok: {len(consts)} GPIO constants match hardware/pip-wiring.yml")

if __name__ == "__main__":
    main()
