#!/bin/bash
# Bench acceptance for the wow demo: health of the four machines, every scene once,
# the tour N times unattended, the log tail. Prints a markdown-ish transcript to stdout.
# Usage: scripts/acceptance.sh [--tours 3] [--skip-scenes]
set -uo pipefail
BRAIN=http://pizerow.local:8080
BODY=http://192.168.1.110
CORTEX=http://orin-desktop.local:8090
VOICE=http://pi5.local:8091
TOURS=3; SCENES=1
while [ $# -gt 0 ]; do case "$1" in --tours) TOURS=$2; shift 2;; --skip-scenes) SCENES=0; shift;; *) echo "unknown $1"; exit 2;; esac; done
ts() { date +%H:%M:%S; }
scene_wait() {  # wait until the director is idle (health.scene == ""), max $1 s
    local max=$1 t=0
    while [ $t -lt $max ]; do
        s=$(curl -s --max-time 3 $BRAIN/health | python3 -c 'import sys,json; print(json.load(sys.stdin).get("scene",""))' 2>/dev/null)
        [ -z "$s" ] && return 0
        sleep 2; t=$((t+2))
    done
    echo "  (still in scene after ${max}s)"; return 1
}
echo "# acceptance $(date +%F' '%T)"
echo "## health"
echo "- body /senses: $(curl -s --max-time 3 $BODY/senses)"
echo "- brain /health: $(curl -s --max-time 3 $BRAIN/health)"
echo "- cortex /health: $(curl -s --max-time 5 $CORTEX/health)"
echo "- voice /health: $(curl -s --max-time 5 $VOICE/health)"
if [ $SCENES = 1 ]; then
  echo "## scenes"
  for sc in reflex night fallback fever who; do
    echo "- $(ts) start $sc: $(curl -s -X POST $BRAIN/scene -H 'content-type: application/json' -d "{\"name\":\"$sc\"}")"
    scene_wait 120
    echo "  $(ts) end $sc; health: $(curl -s --max-time 3 $BRAIN/health)"
  done
fi
echo "## tour x$TOURS"
for i in $(seq 1 $TOURS); do
  echo "- $(ts) tour $i start: $(curl -s -X POST $BRAIN/scene -H 'content-type: application/json' -d '{"name":"tour"}')"
  scene_wait 240
  echo "  $(ts) tour $i end; health: $(curl -s --max-time 3 $BRAIN/health)"
done
echo "## log tail (notes, reflex, llm)"
curl -s "$BRAIN/log?n=400" | python3 -c '
import sys,json
for e in json.load(sys.stdin):
    if e["kind"] in ("note","reflex","llm","event","tool"):
        print("  ", e["kind"], e.get("name",""), e.get("micros",""), (e.get("detail","") or "")[:110])
'
