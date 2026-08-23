#!/bin/bash
# Copy the Pi 5 build to the Pi Zero and (re)start it as a systemd user service.
# Usage: deploy-zero.sh [--pip URL] [--llm URL] [--host pi@pizerow.local] [--bin path]
set -euo pipefail
HOST=pi@pizerow.local; PIP=http://192.168.1.110; LLM=http://pi5.local:8081; BIN=""
while [ $# -gt 0 ]; do case "$1" in --pip) PIP=$2; shift 2;; --llm) LLM=$2; shift 2;; --host) HOST=$2; shift 2;; --bin) BIN=$2; shift 2;; *) echo "unknown $1"; exit 2;; esac; done
HERE="$(cd "$(dirname "$0")/.." && pwd)"
[ -n "$BIN" ] || BIN="$HERE/build-pi5/pip-brain"
[ -f "$BIN" ] || { echo "no binary at $BIN; build on the Pi 5 first or pass --bin"; exit 1; }
ssh "$HOST" 'mkdir -p ~/pip-brain ~/.config/systemd/user'
scp -q "$BIN" "$HOST:~/pip-brain/pip-brain.new"
sed -e "s#--pip [^ ]*#--pip $PIP#" -e "s#--llm [^ ]*#--llm $LLM#" "$HERE/deploy/pip-brain.service" | ssh "$HOST" 'cat > ~/.config/systemd/user/pip-brain.service'
ssh "$HOST" 'mv ~/pip-brain/pip-brain.new ~/pip-brain/pip-brain && chmod +x ~/pip-brain/pip-brain && systemctl --user daemon-reload && systemctl --user enable pip-brain >/dev/null 2>&1; systemctl --user restart pip-brain && sleep 2 && systemctl --user --no-pager status pip-brain | head -5; loginctl enable-linger $USER 2>/dev/null || echo "note: enable-linger needs sudo; service runs while pi is logged in, or run: sudo loginctl enable-linger pi"'
ssh "$HOST" 'curl -s -m 3 http://127.0.0.1:8080/health; echo'
