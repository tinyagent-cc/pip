#!/bin/bash
# Copy the Pi 5 build to the Pi Zero and (re)start it as a systemd user service.
#
# Usage: deploy-zero.sh [--link DEV] [--pip URL] [--llm URL] [--llm2 URL]
#                       [--cortex URL] [--voice URL] [--model NAME]
#                       [--listen-port N] [--host pi@pizerow.local] [--bin path]
#
# The unit's ExecStart is generated here from these values rather than
# sed-patched out of deploy/pip-brain.service: v1 has too many flags for
# patching to stay honest. The rest of the unit comes from that file verbatim.
set -euo pipefail
HOST=pi@pizerow.local
BIN=""
LINK=/dev/ttyAMA0
PIP=http://192.168.1.110
LLM=http://orin-desktop.local:8081
LLM2=http://pi5.local:8081
CORTEX=http://orin-desktop.local:8090
VOICE=http://pi5.local:8091
MODEL=Qwen2.5-3B-Instruct
PORT=8080
while [ $# -gt 0 ]; do
  case "$1" in
    --link) LINK=$2; shift 2;;
    --pip) PIP=$2; shift 2;;
    --llm) LLM=$2; shift 2;;
    --llm2) LLM2=$2; shift 2;;
    --cortex) CORTEX=$2; shift 2;;
    --voice) VOICE=$2; shift 2;;
    --model) MODEL=$2; shift 2;;
    --listen-port) PORT=$2; shift 2;;
    --host) HOST=$2; shift 2;;
    --bin) BIN=$2; shift 2;;
    *) echo "unknown $1"; exit 2;;
  esac
done
HERE="$(cd "$(dirname "$0")/.." && pwd)"
[ -n "$BIN" ] || BIN="$HERE/build-pi5/pip-brain"
[ -f "$BIN" ] || { echo "no binary at $BIN; build on the Pi 5 first or pass --bin"; exit 1; }

# Every URL flag is optional: an empty value drops the flag, which is how you
# deploy with no cortex, no voice, or over HTTP with the UART unplugged.
EXEC="%h/pip-brain/pip-brain"
[ -n "$LINK" ] && EXEC="$EXEC --link $LINK"
[ -n "$PIP" ] && EXEC="$EXEC --pip $PIP"
EXEC="$EXEC --listen-port $PORT"
[ -n "$LLM" ] && EXEC="$EXEC --llm $LLM"
[ -n "$LLM2" ] && EXEC="$EXEC --llm2 $LLM2"
EXEC="$EXEC --model $MODEL"
[ -n "$CORTEX" ] && EXEC="$EXEC --cortex $CORTEX"
[ -n "$VOICE" ] && EXEC="$EXEC --voice $VOICE"

ssh "$HOST" 'mkdir -p ~/pip-brain ~/.config/systemd/user'
scp -q "$BIN" "$HOST:~/pip-brain/pip-brain.new"
grep -v '^ExecStart=' "$HERE/deploy/pip-brain.service" \
  | sed "s#^\[Service\]#[Service]\nExecStart=$EXEC#" \
  | ssh "$HOST" 'cat > ~/.config/systemd/user/pip-brain.service'
ssh "$HOST" 'mv ~/pip-brain/pip-brain.new ~/pip-brain/pip-brain && chmod +x ~/pip-brain/pip-brain && systemctl --user daemon-reload && systemctl --user enable pip-brain >/dev/null 2>&1; systemctl --user restart pip-brain && sleep 2 && systemctl --user --no-pager status pip-brain | head -5; loginctl enable-linger $USER 2>/dev/null || echo "note: enable-linger needs sudo; service runs while pi is logged in, or run: sudo loginctl enable-linger pi"'
ssh "$HOST" "curl -s -m 3 http://127.0.0.1:$PORT/health; echo"
