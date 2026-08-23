#!/bin/bash
# Build pip-brain on the Pi 5 (aarch64, Debian 13). The binary runs unchanged on the Pi Zero 2 W.
set -euo pipefail
export PATH="$HOME/tools/cmake-4.4.2-linux-aarch64/bin:$PATH"
SRC="$(cd "$(dirname "$0")/.." && pwd)"
TA="${TINY_AGENT_DIR:-$HOME/tiny_agent_cpp}"
RETE="${PIP_RETE_DIR:-$TA/build-reflex/_deps/rete_cpp-src}"
[ -d "$RETE" ] || RETE=""   # empty: CMake fetches the pinned rete_cpp
cmake -S "$SRC" -B "$SRC/build-pi5" -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$HOME/tools/vcpkg/scripts/buildsystems/vcpkg.cmake" \
  -DTINY_AGENT_DIR="$TA" -DPIP_RETE_DIR="$RETE" -DPIP_BRAIN_BUILD_TESTS=ON
cmake --build "$SRC/build-pi5" -j4
ctest --test-dir "$SRC/build-pi5" --output-on-failure
ls -la "$SRC/build-pi5/pip-brain"
