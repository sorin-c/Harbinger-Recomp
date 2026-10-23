#!/usr/bin/env bash
# Run the original Harbinger.exe headless under Wine (Linux, no GPU needed) so
# the rebuild can be compared against it. Captures screenshots and, with
# TRACE=1, a syscall log that map-load-order.py turns into a resource load order.
#
#   GAME=/path/to/Harbinger OUT=/tmp/run SECONDS_TO_RUN=60 TRACE=1 ./run-original.sh
#
# Needs: wine (with wine32), xvfb, imagemagick (import), strace for TRACE=1.
# Notes from getting it to boot:
#   - Leave out dgVoodoo's DDraw.dll/D3D8.dll/D3D9.dll/D3DImm.dll; Wine's own ddraw works.
#   - The game wants to switch display modes: use a Wine virtual desktop.
#   - With no sound card Miles crashes. On a headless box, NULL_AUDIO=1 points
#     ALSA at a null device by writing /etc/asound.conf (only if it doesn't exist).
set -euo pipefail
: "${GAME:?set GAME to the install folder}"
OUT=${OUT:-/tmp/harbinger-run}
SECONDS_TO_RUN=${SECONDS_TO_RUN:-60}
mkdir -p "$OUT/game"
export WINEPREFIX=${WINEPREFIX:-$OUT/wineprefix} WINEDEBUG=${WINEDEBUG:--all} DISPLAY=${DISPLAY_NUM:-:99}

# Private copy of the game without the dgVoodoo wrappers (the game writes ini/saves).
for f in Data Harbinger.exe Harbinger.ini binkw32.dll mss32.dll config.cfg res0.sqh res1.sqh save; do
  [ -e "$OUT/game/$f" ] || cp -r "$GAME/$f" "$OUT/game/"
done

if [ "${NULL_AUDIO:-0}" = 1 ] && [ ! -f /etc/asound.conf ]; then echo 'pcm.!default { type null }' > /etc/asound.conf; fi
pgrep -x Xvfb >/dev/null || { Xvfb "$DISPLAY" -screen 0 1024x768x24 >/dev/null 2>&1 & sleep 2; }
[ -d "$WINEPREFIX" ] || wineboot -i >/dev/null 2>&1
wine reg add 'HKCU\Software\Wine\Explorer' /v Desktop /d Default /f >/dev/null 2>&1
wine reg add 'HKCU\Software\Wine\Explorer\Desktops' /v Default /d 1024x768 /f >/dev/null 2>&1
wineserver -w

cd "$OUT/game"
if [ "${TRACE:-0}" = 1 ]; then
  strace -f -tt -y -e trace=openat,pread64,read,_llseek -o "$OUT/strace.log" wine Harbinger.exe >/dev/null 2>&1 &
else
  wine Harbinger.exe >"$OUT/wine.log" 2>&1 &
fi
for ((t = 5; t <= SECONDS_TO_RUN; t += 5)); do
  sleep 5
  import -window root -display "$DISPLAY" "$OUT/frame_$(printf %03d $t).png" 2>/dev/null || true
done
wineserver -k || true
echo "screenshots in $OUT"
