#!/usr/bin/env bash
# Run the original ASTRO.exe under Wine as a behavioural reference.
# Uses a dedicated prefix (re/wineprefix) and a 640x480 Wine virtual desktop so
# the DirectDraw exclusive-mode switch does not touch the host display mode.
# The desktop is configured in the prefix's registry: `wine explorer
# /desktop=...,640x480 ASTRO.exe` no longer starts the program (Wine 11).
#
# Usage: tools/run_original.sh [extracted-dir]   (default: extracted)
#
# For automated captures run it on a private X server, e.g.
#   Xvfb :99 -screen 0 640x480x24 &  DISPLAY=:99 tools/run_original.sh
# and grab frames with ffmpeg -f x11grab (see docs/port.md, "Checking against
# the original").
set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"
data="$(cd "${1:-$repo/extracted}/game" && pwd)"
export WINEPREFIX="${WINEPREFIX:-$repo/re/wineprefix}"
export WINEDEBUG="${WINEDEBUG:--all}"

if [ ! -d "$WINEPREFIX" ]; then
    echo "Creating Wine prefix in $WINEPREFIX"
    WINEDLLOVERRIDES="mscoree,mshtml=" wineboot -i >/dev/null 2>&1
fi

wine reg add 'HKCU\Software\Wine\Explorer' /v Desktop /t REG_SZ /d AstroAssembler /f >/dev/null 2>&1
wine reg add 'HKCU\Software\Wine\Explorer\Desktops' /v AstroAssembler /t REG_SZ /d 640x480 /f >/dev/null 2>&1

cd "$data"
exec wine ASTRO.exe
