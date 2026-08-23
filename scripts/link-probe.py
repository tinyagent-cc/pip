#!/usr/bin/env python3
"""Talk to Pip's body over the UART link, with nothing but the standard library.

Run it on whatever is wired to the Pico (the Pi Zero, /dev/ttyAMA0):

    python3 scripts/link-probe.py                       # the default demo sequence
    python3 scripts/link-probe.py --watch --listen 10    # just watch what the body says
    python3 scripts/link-probe.py --tone 2               # stream 2 s of 440 Hz as AUDIO frames
    python3 scripts/link-probe.py '{"cmd":"ping"}' '{"cmd":"express","emotion":"wink"}'

Frame format is PROTOCOL.md v1: 0xA5 | type | len u16 LE | payload | crc8.
"""
import argparse
import math
import os
import struct
import sys
import termios
import time

SYNC = 0xA5
TYPE_JSON = 0x01
TYPE_AUDIO = 0x02
MAX_PAYLOAD = 512


def crc8(data, crc=0):
    """CRC-8 of the catalogue: poly 0x07, MSB first, init 0, no reflection, no xorout."""
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = ((crc << 1) ^ 0x07) & 0xFF if crc & 0x80 else (crc << 1) & 0xFF
    return crc


def encode(frame_type, payload):
    if len(payload) > MAX_PAYLOAD:
        raise ValueError("payload over %d bytes" % MAX_PAYLOAD)
    head = bytes([frame_type, len(payload) & 0xFF, len(payload) >> 8])
    return bytes([SYNC]) + head + payload + bytes([crc8(head + payload)])


class Decoder:
    """Byte-at-a-time, resyncs on anything it cannot make sense of. Mirrors core/src/link.cpp."""

    def __init__(self):
        self.reset()
        self.frames = 0
        self.bad = 0

    def reset(self):
        self.state = "sync"
        self.buf = bytearray()
        self.length = 0
        self.crc = 0
        self.type = 0

    def push(self, b):
        if self.state == "sync":
            if b == SYNC:
                self.state = "type"
            return None
        if self.state == "type":
            if b not in (TYPE_JSON, TYPE_AUDIO):
                self.bad += 1
                self.reset()
                if b == SYNC:
                    self.state = "type"
                return None
            self.type = b
            self.crc = crc8([b])
            self.state = "len0"
            return None
        if self.state == "len0":
            self.length = b
            self.crc = crc8([b], self.crc)
            self.state = "len1"
            return None
        if self.state == "len1":
            self.length |= b << 8
            if self.length > MAX_PAYLOAD:
                self.bad += 1
                self.reset()
                if b == SYNC:
                    self.state = "type"
                return None
            self.crc = crc8([b], self.crc)
            self.buf = bytearray()
            self.state = "payload" if self.length else "crc"
            return None
        if self.state == "payload":
            self.buf.append(b)
            self.crc = crc8([b], self.crc)
            if len(self.buf) == self.length:
                self.state = "crc"
            return None
        # crc
        want, kind, payload = self.crc, self.type, bytes(self.buf)
        self.reset()
        if b != want:
            self.bad += 1
            return None
        self.frames += 1
        return (kind, payload)


def open_port(path, baud):
    fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    speed = getattr(termios, "B%d" % baud, None)
    if speed is None:
        os.close(fd)
        raise SystemExit("no termios constant for baud %d" % baud)
    attrs = termios.tcgetattr(fd)
    cc = attrs[6]
    cc[termios.VMIN] = 0
    cc[termios.VTIME] = 0
    # Raw 8N1, no flow control, no modem lines: the wire is three jumpers.
    termios.tcsetattr(fd, termios.TCSANOW,
                      [0, 0, termios.CS8 | termios.CREAD | termios.CLOCAL, 0, speed, speed, cc])
    termios.tcflush(fd, termios.TCIOFLUSH)
    return fd


def pump(fd, dec, seconds, quiet=False):
    """Read for `seconds`, printing every frame that decodes. Returns the frames seen."""
    seen = []
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        try:
            chunk = os.read(fd, 4096)
        except BlockingIOError:
            chunk = b""
        if not chunk:
            time.sleep(0.005)
            continue
        for b in chunk:
            got = dec.push(b)
            if got is None:
                continue
            kind, payload = got
            seen.append(got)
            if quiet:
                continue
            if kind == TYPE_JSON:
                print("<- %s" % payload.decode("utf-8", "replace"))
            else:
                print("<- audio %d bytes" % len(payload))
    return seen


def stream_tone(fd, dec, seconds, hz, rate=16000, per=256, amp=12000):
    """Send a sine as type 0x02 AUDIO frames, paced at real time like the brain would.

    s16 mono at `rate`; `per` samples a frame is 512 bytes, the protocol's maximum payload
    and 16 ms of sound. Keeps draining RX between frames so senses frames still print.
    """
    total = int(rate * seconds)
    step = 2.0 * math.pi * hz / rate
    t0 = time.monotonic()
    i = 0
    sent = 0
    while i < total:
        n = min(per, total - i)
        payload = struct.pack("<%dh" % n, *[int(amp * math.sin(step * (i + k))) for k in range(n)])
        os.write(fd, encode(TYPE_AUDIO, payload))
        sent += 1
        i += n
        due = t0 + i / float(rate)
        while True:
            left = due - time.monotonic()
            if left <= 0:
                break
            pump(fd, dec, min(left, 0.004))
    return sent


DEMO = [
    '{"cmd":"ping"}',
    '{"cmd":"express","emotion":"happy"}',
    '{"cmd":"say","text":"hello from the wire"}',
    '{"cmd":"hud","scene":"reflex","reflex_us":95,"judge_ms":5800,"brain":true,"cortex":true,"mind":"J"}',
]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("commands", nargs="*", help="JSON objects to send; default is a demo sequence")
    ap.add_argument("--port", default="/dev/ttyAMA0")
    ap.add_argument("--baud", type=int, default=921600)
    ap.add_argument("--watch", action="store_true", help="send nothing, only listen")
    ap.add_argument("--listen", type=float, default=3.0, help="seconds to keep reading after the last command")
    ap.add_argument("--gap", type=float, default=1.0, help="seconds between commands")
    ap.add_argument("--tone", type=float, default=0.0, help="seconds of sine to stream as AUDIO frames")
    ap.add_argument("--tone-hz", type=float, default=440.0)
    args = ap.parse_args()

    fd = open_port(args.port, args.baud)
    dec = Decoder()
    try:
        cmds = args.commands or ([] if (args.watch or args.tone) else DEMO)
        for js in cmds:
            print("-> %s" % js)
            os.write(fd, encode(TYPE_JSON, js.encode("utf-8")))
            pump(fd, dec, args.gap)
        if args.tone:
            n = stream_tone(fd, dec, args.tone, args.tone_hz)
            print("-> %d audio frames, %.1f s of %.0f Hz" % (n, args.tone, args.tone_hz))
        pump(fd, dec, args.listen)
    finally:
        os.close(fd)
    print("frames=%d bad=%d" % (dec.frames, dec.bad))
    return 0 if dec.frames else 1


if __name__ == "__main__":
    sys.exit(main())
