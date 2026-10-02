#!/usr/bin/env bash
# Draws the game's key art: a kite over open water.
#
#   Content/Splash/Splash.png, EdSplash.png   startup splash, with the game's name. The engine
#                                             shows EdSplash when the game runs from the editor
#                                             binary and Splash in a packaged build.
#   <art dir>/menu_background.png             the same scene without the name, 1920x1080, which
#                                             import_menu_art.py imports as T_MenuBackground.
#
# When scripts/render-menu-video.sh has filmed the menu video, the scene is a frame of it
# (Saved/MenuVideo/keyframe.png), so the splash, the still background and the video match.
# Otherwise it is drawn: a kite over open water.
#
# Needs ImageMagick 7 (`magick`).
#
#   scripts/editor/make_splash.sh [art directory, default Saved/MenuArt]
#   scripts/editor/make_splash.sh --title-only <out.png> <width> <height>
#       just the name, on a transparent canvas, laid out as on the splash; the intro video
#       brings it in over the film.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
SPLASH_DIR="$PROJECT_DIR/Content/Splash"
FONT="${KITE_FONT:-Adwaita-Sans-Black-Italic}"
KEYFRAME="$PROJECT_DIR/Saved/MenuVideo/keyframe.png"

LIME='#b9e21c'
NAVY='#0c2340'
TEAL='#12a5a0'
WHITE='#f4f4f0'

# The scene is laid out on a 720x370 canvas (the size of the engine's own splash) and drawn at
# any whole multiple of it.
scene() {
    local scale="$1" out="$2"
    local w=$((720 * scale)) h=$((370 * scale)) sea_y=$((220 * scale)) sea_h=$((150 * scale))
    local s="scale $scale,$scale"
    magick -size "${w}x${h}" gradient:'#2f7fc4'-'#bfe3f2' \
        \( -size "${w}x${sea_h}" gradient:'#16a6a8'-'#0a4d6e' \) -geometry "+0+${sea_y}" -composite \
        -fill '#ffffff' -draw "$s fill-opacity 0.55 ellipse 150,95 70,16 0,360" \
        -draw "$s fill-opacity 0.45 ellipse 560,70 95,18 0,360" \
        -draw "$s fill-opacity 0.35 ellipse 420,130 60,12 0,360" \
        -stroke "$WHITE" -strokewidth 1 -fill none \
        -draw "$s stroke-opacity 0.7 line 446,78 384,246" \
        -draw "$s stroke-opacity 0.7 line 574,170 386,248" \
        -stroke "$NAVY" -strokewidth 34 -draw "$s bezier 446,78 520,36 610,96 574,170" \
        -stroke "$LIME" -strokewidth 24 -draw "$s bezier 446,78 520,36 610,96 574,170" \
        -stroke "$TEAL" -strokewidth 7 -draw "$s bezier 452,88 518,54 594,104 566,164" \
        -stroke "$NAVY" -strokewidth 5 -draw "$s line 513,50 533,76" -draw "$s line 583,104 559,116" \
        -stroke none \
        -fill "$NAVY" -draw "$s ellipse 376,230 6,6 0,360" \
        -draw "$s polygon 369,236 383,236 391,260 376,265" \
        -draw "$s polygon 377,260 386,260 397,281 389,283" \
        -draw "$s polygon 378,260 371,260 362,281 370,283" \
        -draw "$s polygon 380,240 386,246 384,250 377,245" \
        -fill "$WHITE" -draw "$s polygon 348,282 414,276 416,282 350,288" \
        -fill '#ffffff' -draw "$s fill-opacity 0.55 polygon 344,284 350,283 352,289 344,292" \
        -depth 8 "$out"
}

# The name, laid out on a canvas of the given size: scaled from the 720x370 splash layout by
# height, bottom left, with a soft shadow so it reads over a bright sky or sea.
title() {
    local w="$1" h="$2" out="$3"
    px() { awk -v v="$1" -v h="$h" 'BEGIN { printf "%.0f", v * h / 370 }'; }
    magick -size "${w}x${h}" xc:none \
        -font "$FONT" -gravity SouthWest \
        -fill "$NAVY" -pointsize "$(px 62)" -annotate +"$(px 30)"+"$(px 56)" 'KITESURF' \
        -fill "$NAVY" -pointsize "$(px 22)" -annotate +"$(px 34)"+"$(px 24)" 'koorikla  big air' \
        -channel A -blur 0x"$(px 4)" -evaluate multiply 0.6 +channel \
        -fill "$WHITE" -pointsize "$(px 62)" -annotate +"$(px 28)"+"$(px 58)" 'KITESURF' \
        -fill "$LIME" -pointsize "$(px 22)" -annotate +"$(px 32)"+"$(px 26)" 'koorikla  big air' \
        -depth 8 "$out"
}

if [[ "${1:-}" == "--title-only" ]]; then
    title "$3" "$4" "$2"
    echo "Wrote $2"
    exit 0
fi

ART_DIR="${1:-$PROJECT_DIR/Saved/MenuArt}"
mkdir -p "$SPLASH_DIR" "$ART_DIR"

# The scene at splash size and at menu size. 8 bits per channel throughout: the editor imports a
# 16-bit PNG as linear data, which washes the colours out on screen.
if [[ -f "$KEYFRAME" ]]; then
    echo "Using the menu video frame $KEYFRAME"
    magick "$KEYFRAME" -resize 720x370^ -gravity Center -extent 720x370 -depth 8 "$ART_DIR/scene.png"
    magick "$KEYFRAME" -resize 1920x1080^ -gravity Center -extent 1920x1080 -depth 8 "$ART_DIR/menu_background.png"
else
    scene 1 "$ART_DIR/scene.png"
    # Drawn three times the size and cropped to 16:9.
    scene 3 "$ART_DIR/scene_large.png"
    magick "$ART_DIR/scene_large.png" -gravity Center -crop 1920x1080+0+0 +repage -depth 8 "$ART_DIR/menu_background.png"
fi

# Splash: the scene with the name on it. The menus draw their own text over the background.
title 720 370 "$ART_DIR/title.png"
magick "$ART_DIR/scene.png" "$ART_DIR/title.png" -composite -depth 8 "$SPLASH_DIR/Splash.png"
cp "$SPLASH_DIR/Splash.png" "$SPLASH_DIR/EdSplash.png"

echo "Wrote $SPLASH_DIR/Splash.png, EdSplash.png and $ART_DIR/menu_background.png"
