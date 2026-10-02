#!/usr/bin/env bash
set -euo pipefail

OUT="${1:?usage: make_kite_texture.sh <output.png> [loop|boost]}"
SCHEME="${2:-loop}"
FONT="${KITE_FONT:-sans-serif}"

NAVY='#0c2340'
WHITE='#f4f4f0'
case "$SCHEME" in
    loop)  LIME='#b9e21c'; TEAL='#12a5a0'; TAG='K9' ;;
    boost) LIME='#ff6a13'; TEAL='#3fa7e0'; TAG='B5' ;;
    wave) LIME='#00d1e3'; TEAL='#e300d1'; TAG='W7' ;;
    freestyle) LIME='#cc0000'; TEAL='#ffffff'; TAG='F4' ;;
    *) echo "unknown colour scheme: $SCHEME" >&2; exit 1 ;;
esac

# Create a gradient background
magick -size 2048x1024 gradient:"$LIME"-"$TEAL" \
    -fill "$WHITE" -draw "rectangle 0,0 2048,70" \
    -fill "rgba(0,0,0,0.15)" -draw "polygon 0,0 2048,1024 2048,0" \
    -fill "$NAVY" -draw "polygon 0,70 300,70 110,1024 0,1024" \
    -fill "$NAVY" -draw "polygon 2048,70 1748,70 1938,1024 2048,1024" \
    -fill "$TEAL" -draw "polygon 300,70 390,70 200,1024 110,1024" \
    -fill "$TEAL" -draw "polygon 1748,70 1658,70 1848,1024 1938,1024" \
    -fill "$NAVY" -draw "polygon 930,70 1118,70 1070,1024 978,1024" \
    -fill "$TEAL" -draw "polygon 0,960 2048,960 2048,1024 0,1024" \
     -fill "$NAVY" \
    -draw "translate 600,560 scale 0.72,1 font-size 185 text-anchor middle text 0,0 'koorikla'" \
    -draw "translate 1448,560 scale 0.72,1 font-size 185 text-anchor middle text 0,0 'koorikla'" \
    -fill "$WHITE" \
    -draw "translate 1024,330 scale 0.72,1 font-size 150 text-anchor middle text 0,0 '12'" \
    -draw "translate 150,760 scale 0.72,1 font-size 90 text-anchor middle text 0,0 '$TAG'" \
    -draw "translate 1898,760 scale 0.72,1 font-size 90 text-anchor middle text 0,0 '$TAG'" \
    "$OUT"
echo "Wrote $OUT"
