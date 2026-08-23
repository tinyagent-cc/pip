#!/usr/bin/env python3
"""Poke the body over the UART and print what comes back.

    link-probe.py /dev/ttyAMA0 '{"cmd":"express","emotion":"happy"}' [--baud N] [--seconds N]

Sends one JSON frame, then decodes every frame that arrives for two seconds.
Standard library only, so it runs on the Zero with nothing installed. Stop
pip-brain first: two readers on one UART each get half the bytes.
"""
import argparse
import json
import os
import sys
import termios
import time

SYNC = 0xA5
TYPE_JSON = 1
TYPE_AUDIO = 2
MAX_PAYLOAD = 512


def crc8(data, crc=0):
    """Poly 0x07, MSB first, init 0. crc8(b"123456789") == 0xF4."""
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = ((crc << 1) ^ 0x07) & 0xFF if crc & 0x80 else (crc << 1) & 0xFF
    return crc


def encode(frame_type, payload):
    if len(payload) > MAX_PAYLOAD:
        raise ValueError(f"payload {len(payload)} > {MAX_PAYLOAD}")
    header = bytes([frame_type, len(payload) & 0xFF, (len(payload) >> 8) & 0xFF])
    body = header + payload
    return bytes([SYNC]) + body + bytes([crc8(body)])


def decode_stream(chunks):
    """Yields (type, payload) for every good frame; resyncs on anything else."""
    buf = bytearray()
    for chunk in chunks:
        buf.extend(chunk)
        while True:
            start = buf.find(bytes([SYNC]))
            if start < 0:
                buf.clear()
                break
            if start:
                del buf[:start]
            if len(buf) < 5:
                break
            ftype = buf[1]
            length = buf[2] | (buf[3] << 8)
            if ftype not in (TYPE_JSON, TYPE_AUDIO) or length > MAX_PAYLOAD:
                del buf[:1]          # bad header: this 0xA5 was not a sync byte
                continue
            if len(buf) < 5 + length:
                break
            payload = bytes(buf[4:4 + length])
            if crc8(bytes(buf[1:4 + length])) != buf[4 + length]:
                print("bad crc, resyncing", file=sys.stderr)
                del buf[:1]
                continue
            del buf[:5 + length]
            yield ftype, payload


def open_port(dev, baud):
    fd = os.open(dev, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    attrs = termios.tcgetattr(fd)
    speed = getattr(termios, f"B{baud}", None)
    if speed is None:
        os.close(fd)
        raise SystemExit(f"link-probe: this Python has no B{baud} constant")
    iflag, oflag, cflag, lflag, ispeed, ospeed, cc = attrs
    iflag = 0
    oflag = 0
    lflag = 0
    cflag = (cflag & ~(termios.CSIZE | termios.PARENB | termios.CSTOPB)) | termios.CS8 | termios.CLOCAL | termios.CREAD
    cc = list(cc)
    cc[termios.VMIN] = 0
    cc[termios.VTIME] = 1
    termios.tcsetattr(fd, termios.TCSANOW, [iflag, oflag, cflag, lflag, speed, speed, cc])
    termios.tcflush(fd, termios.TCIOFLUSH)
    return fd


def main():
    ap = argparse.ArgumentParser(description="send one frame to Pip's body and print what it says back")
    ap.add_argument("device")
    ap.add_argument("payload", nargs="?", default='{"cmd":"ping"}')
    ap.add_argument("--baud", type=int, default=921600)
    ap.add_argument("--seconds", type=float, default=2.0)
    args = ap.parse_args()

    obj = json.loads(args.payload)          # fail here rather than on the wire
    fd = open_port(args.device, args.baud)
    os.write(fd, encode(TYPE_JSON, json.dumps(obj, separators=(",", ":")).encode()))
    print(f"sent {obj}")

    def chunks():
        deadline = time.monotonic() + args.seconds
        while time.monotonic() < deadline:
            try:
                data = os.read(fd, 1024)
            except BlockingIOError:
                data = b""
            if data:
                yield data
            else:
                time.sleep(0.005)

    seen = 0
    for ftype, payload in decode_stream(chunks()):
        seen += 1
        if ftype == TYPE_JSON:
            print(f"json  {payload.decode('utf-8', 'replace')}")
        else:
            print(f"audio {len(payload)} bytes")
    os.close(fd)
    print(f"{seen} frame(s) in {args.seconds:g}s")
    return 0


if __name__ == "__main__":
    sys.exit(main())
