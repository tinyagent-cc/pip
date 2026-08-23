#!/usr/bin/env bash
# Runs ON the Pi 5 (pi@pi5.local). No sudo. Installs the pip-voice user unit on
# :8091 and waits for health. Never touches :8080 (Riadh's llama-server) or
# :8081 (the brain's fallback LLM).
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
UNIT_DIR="$HOME/.config/systemd/user"
VENV="$HOME/pip-probe/venv"
VOICE="${PIP_PIPER_VOICE:-$HOME/pip-probe/voices/en_US-lessac-medium.onnx}"

log() { printf '\033[36m==>\033[0m %s\n' "$*"; }

[ -x "$VENV/bin/python" ] || { echo "missing venv: $VENV" >&2; exit 1; }
[ -f "$VOICE" ]      || { echo "missing voice: $VOICE" >&2; exit 1; }
[ -f "$VOICE.json" ] || { echo "missing voice config: $VOICE.json" >&2; exit 1; }
"$VENV/bin/python" -c 'import piper, numpy, fastapi, uvicorn' \
  || { echo "venv is missing piper/numpy/fastapi/uvicorn" >&2; exit 1; }

mkdir -p "$UNIT_DIR"
install -m 0644 "$HERE/pip-voice.service" "$UNIT_DIR/pip-voice.service"
systemctl --user daemon-reload
systemctl --user enable --now pip-voice.service
systemctl --user restart pip-voice.service

for _ in $(seq 1 45); do
  if curl -sf -m 2 http://127.0.0.1:8091/health >/dev/null 2>&1; then
    log "pip-voice up: $(curl -s -m 2 http://127.0.0.1:8091/health)"
    exit 0
  fi
  sleep 2
done

echo "TIMEOUT waiting for pip-voice on :8091" >&2
systemctl --user status pip-voice.service --no-pager -l | tail -30 >&2 || true
exit 1
