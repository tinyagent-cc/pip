#!/usr/bin/env bash
# Runs on the Mac. Ships services/voice to the Pi 5, installs the pip-voice user
# unit (:8091), waits for health, then speaks one line and wraps the raw PCM
# into a WAV so it can be played locally.
set -euo pipefail

HOST="${PIP_PI5_HOST:-pi@pi5.local}"
NAME="${HOST#*@}"
SRC="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/voice/"
OUT="${PIP_TTS_OUT:-/tmp/pip.pcm}"
WAV="${OUT%.pcm}.wav"

echo "==> rsync $SRC -> $HOST:~/pip-voice/"
rsync -a --delete --exclude '__pycache__' --exclude '.pytest_cache' \
  "$SRC" "$HOST:~/pip-voice/"

echo "==> install-pi5.sh"
ssh "$HOST" 'bash ~/pip-voice/deploy/install-pi5.sh'

echo "==> health from the Mac"
curl -sf -m 5 "http://$NAME:8091/health"; echo

echo "==> /tts smoke"
curl -s -m 30 -X POST "http://$NAME:8091/tts" \
  -H 'Content-Type: application/json' \
  -d '{"text":"Hello, I am Pip."}' \
  -D /tmp/pip-tts-headers.txt -o "$OUT"
grep -i -E '^(x-sample-rate|x-duration-ms|x-synth-ms|content-type)' /tmp/pip-tts-headers.txt
ls -la "$OUT"

python3 - "$OUT" "$WAV" <<'PY'
import sys, wave
raw = open(sys.argv[1], "rb").read()
with wave.open(sys.argv[2], "wb") as w:
    w.setnchannels(1); w.setsampwidth(2); w.setframerate(16000)
    w.writeframes(raw)
print(f"wrote {sys.argv[2]}: {len(raw)//2} samples, {len(raw)/2/16000:.2f}s")
PY

echo "==> play it: afplay $WAV   (or: ffplay -f s16le -ar 16000 -ac 1 $OUT)"
