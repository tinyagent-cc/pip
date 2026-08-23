#!/bin/bash
# Fill firmware/config.h's WiFi SSID and password from the Mac this runs on.
# Reads the current network's name and its keychain password; prints neither.
# config.h is git-ignored, so the values stay on this machine.
set -euo pipefail
cfg="$(cd "$(dirname "$0")/.." && pwd)/firmware/config.h"
[ -f "$cfg" ] || cp "$(dirname "$cfg")/config.example.h" "$cfg"

wif="$(networksetup -listallhardwareports | awk '/Wi-Fi/{getline; print $2; exit}')"
wif="${wif:-en0}"
ssid="$(ipconfig getsummary "$wif" 2>/dev/null | awk -F': ' '/^ +SSID/ {print $2; exit}')"
[ -n "$ssid" ] || ssid="$(networksetup -getairportnetwork "$wif" 2>/dev/null | sed 's/^[^:]*: //')"
[ -n "$ssid" ] || { read -r -p "WiFi SSID (2.4 GHz network): " ssid; }

pass=""
for kc in "" /Library/Keychains/System.keychain; do
  for sel in "-D AirPort\ network\ password -a" "-a" "-l"; do
    # shellcheck disable=SC2086
    pass="$(eval security find-generic-password $sel "\"\$ssid\"" $kc -w 2>/dev/null || true)"
    [ -n "$pass" ] && break 2
  done
done
if [ -z "$pass" ]; then
  read -r -s -p "Password for that network (not echoed, not stored anywhere but config.h): " pass; echo
fi
[ -n "$pass" ] || { echo "no password given" >&2; exit 1; }

SSID="$ssid" PASS="$pass" python3 - "$cfg" <<'PY'
import os, re, sys
p = sys.argv[1]; s = open(p).read()
def c(v): return '"' + v.replace("\\", "\\\\").replace('"', '\\"') + '"'
s, n1 = re.subn(r'(#define PIP_WIFI_SSID )".*"', lambda m: m.group(1) + c(os.environ["SSID"]), s)
s, n2 = re.subn(r'(#define PIP_WIFI_PASS )".*"', lambda m: m.group(1) + c(os.environ["PASS"]), s)
assert n1 == 1 and n2 == 1, "PIP_WIFI_SSID/PASS lines not found"
open(p, "w").write(s)
print("config.h: SSID (%d chars) and password (%d chars) set" % (len(os.environ["SSID"]), len(os.environ["PASS"])))
PY
chmod 600 "$cfg"
