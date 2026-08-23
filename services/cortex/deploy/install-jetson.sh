#!/usr/bin/env bash
# Runs ON the Jetson (orin@orin-desktop.local). No sudo anywhere.
# Installs the systemd *user* units for the text LLM (:8081) and the VLM (:8082),
# downloading the SmolVLM GGUF pair if it is missing, then starts them and
# prints their health. The cortex unit (:8090) is installed by the second half.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
UNIT_DIR="$HOME/.config/systemd/user"
MODELS="$HOME/models"
VLM_DIR="$MODELS/smolvlm"

log() { printf '\033[36m==>\033[0m %s\n' "$*"; }

# --- models ------------------------------------------------------------------
if [ ! -f "$VLM_DIR/SmolVLM-500M-Instruct-Q8_0.gguf" ] || \
   [ ! -f "$VLM_DIR/mmproj-SmolVLM-500M-Instruct-f16.gguf" ]; then
  log "downloading SmolVLM-500M-Instruct GGUF into $VLM_DIR"
  PATH="$HOME/.local/bin:$PATH" hf download ggml-org/SmolVLM-500M-Instruct-GGUF \
    --local-dir "$VLM_DIR"
fi
for f in "$MODELS/Qwen2.5-3B-Instruct-Q4_K_M.gguf" \
         "$VLM_DIR/SmolVLM-500M-Instruct-Q8_0.gguf" \
         "$VLM_DIR/mmproj-SmolVLM-500M-Instruct-f16.gguf"; do
  [ -f "$f" ] || { echo "missing model: $f" >&2; exit 1; }
done

# --- units -------------------------------------------------------------------
mkdir -p "$UNIT_DIR"
install -m 0644 "$HERE/llama-text.service" "$UNIT_DIR/llama-text.service"
install -m 0644 "$HERE/llama-vlm.service"  "$UNIT_DIR/llama-vlm.service"

CORTEX_UNIT=0
if [ -f "$HERE/pip-cortex.service" ]; then
  install -m 0644 "$HERE/pip-cortex.service" "$UNIT_DIR/pip-cortex.service"
  CORTEX_UNIT=1
fi

install -m 0755 "$HERE/pip-llm" "$HOME/bin/pip-llm" 2>/dev/null || {
  mkdir -p "$HOME/bin"; install -m 0755 "$HERE/pip-llm" "$HOME/bin/pip-llm"; }

systemctl --user daemon-reload
systemctl --user enable --now llama-text.service llama-vlm.service
[ "$CORTEX_UNIT" = 1 ] && systemctl --user enable --now pip-cortex.service

# --- wait for health ---------------------------------------------------------
wait_health() {
  local url="$1" name="$2" tries="${3:-90}"
  for _ in $(seq 1 "$tries"); do
    if curl -sf -m 2 "$url" >/dev/null 2>&1; then
      log "$name up: $(curl -s -m 2 "$url")"
      return 0
    fi
    sleep 2
  done
  echo "TIMEOUT waiting for $name at $url" >&2
  systemctl --user status "$name" --no-pager -l | tail -20 >&2 || true
  return 1
}

wait_health http://127.0.0.1:8081/health llama-text.service
wait_health http://127.0.0.1:8082/health llama-vlm.service
[ "$CORTEX_UNIT" = 1 ] && wait_health http://127.0.0.1:8090/health pip-cortex.service

log "done"
