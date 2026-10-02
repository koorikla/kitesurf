#!/usr/bin/env bash
# Draws a kite canopy texture: colour blocking and the "koorikla" wordmark.
# The image maps onto the canopy with the leading edge along the top and the wingtips at the
# left and right edges. Needs ImageMagick 7 (`magick`).
#
#   scripts/editor/make_kite_texture.sh <output.png> [loop|boost]
#
# Each kite model has its own colours, so the two read apart on the water and in the gear preview.
# A new colourway is a new case below and a new canopy material in create_materials.py.
set -euo pipefail

OUT="${1:?usage: make_kite_texture.sh <output.png> [loop|boost]}"
SCHEME="${2:-loop}"
FONT="${KITE_FONT:-Adwaita-Sans-Black-Italic}"

NAVY='#0c2340'
WHITE='#f4f4f0'
case "$SCHEME" in
    # The loop kite: lime with navy and teal.
    loop)  LIME='#b9e21c'; TEAL='#12a5a0'; TAG='K9' ;;
    # The boost kite: hot orange with navy and sky blue.
    boost) LIME='#ff6a13'; TEAL='#3fa7e0'; TAG='B5' ;;
    *) echo "unknown colour scheme: $SCHEME" >&2; exit 1 ;;
esac

# The canopy is about 2.6 times as long as its centre chord and the image is 2:1, so text is
# drawn narrow (scale 0.72 in x) to come out with natural proportions on the kite.
magick -size 2048x1024 "xc:$LIME" \
    -fill "$WHITE" -draw "rectangle 0,0 2048,70" \
    -fill "$NAVY" -draw "polygon 0,70 300,70 110,1024 0,1024" \
    -fill "$NAVY" -draw "polygon 2048,70 1748,70 1938,1024 2048,1024" \
    -fill "$TEAL" -draw "polygon 300,70 390,70 200,1024 110,1024" \
    -fill "$TEAL" -draw "polygon 1748,70 1658,70 1848,1024 1938,1024" \
    -fill "$NAVY" -draw "polygon 930,70 1118,70 1070,1024 978,1024" \
    -fill "$TEAL" -draw "polygon 0,960 2048,960 2048,1024 0,1024" \
    -font "$FONT" -fill "$NAVY" \
    -draw "translate 600,560 scale 0.72,1 font-size 185 text-anchor middle text 0,0 'koorikla'" \
    -draw "translate 1448,560 scale 0.72,1 font-size 185 text-anchor middle text 0,0 'koorikla'" \
    -fill "$WHITE" \
    -draw "translate 1024,330 scale 0.72,1 font-size 150 text-anchor middle text 0,0 '12'" \
    -draw "translate 150,760 scale 0.72,1 font-size 90 text-anchor middle text 0,0 '$TAG'" \
    -draw "translate 1898,760 scale 0.72,1 font-size 90 text-anchor middle text 0,0 '$TAG'" \
    "$OUT"
echo "Wrote $OUT"
