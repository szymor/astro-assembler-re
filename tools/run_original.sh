#!/usr/bin/env bash
# Run the original ASTRO.exe under Wine as a behavioural reference.
# Uses a dedicated prefix (re/wineprefix) and a 640x480 virtual desktop so the
# DirectDraw exclusive-mode switch does not touch the host display mode.
#
# Usage: tools/run_original.sh [extracted-dir]   (default: extracted)
set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"
data="$(cd "${1:-$repo/extracted}/game" && pwd)"
export WINEPREFIX="${WINEPREFIX:-$repo/re/wineprefix}"
export WINEDEBUG="${WINEDEBUG:--all}"

if [ ! -d "$WINEPREFIX" ]; then
    echo "Creating Wine prefix in $WINEPREFIX"
    WINEDLLOVERRIDES="mscoree,mshtml=" wineboot -i >/dev/null 2>&1
fi

cd "$data"
exec wine explorer "/desktop=AstroAssembler,640x480" ASTRO.exe
