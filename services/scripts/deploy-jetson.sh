#!/usr/bin/env bash
# Runs on the Mac. Ships services/cortex to the Jetson, installs the three user
# units (llama-text :8081, llama-vlm :8082, pip-cortex :8090), waits for health.
set -euo pipefail

HOST="${PIP_JETSON_HOST:-orin@orin-desktop.local}"
NAME="${HOST#*@}"
SRC="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/cortex/"

echo "==> rsync $SRC -> $HOST:~/pip-cortex/"
rsync -a --delete --exclude '__pycache__' --exclude '.pytest_cache' \
  "$SRC" "$HOST:~/pip-cortex/"

echo "==> install-jetson.sh"
ssh "$HOST" 'bash ~/pip-cortex/deploy/install-jetson.sh'

echo "==> health from the Mac"
curl -sf -m 5 "http://$NAME:8090/health"; echo
