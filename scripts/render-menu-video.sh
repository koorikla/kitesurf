#!/usr/bin/env bash
# Films the menu videos in the game itself and encodes them:
#
#   Content/Movies/MenuLoop.webm   the main menu background, a seamless loop of cinematic shots
#   Content/Movies/Intro.webm      the startup intro: a shot of the rider with the title coming in
#   Saved/MenuVideo/keyframe.png   one frame of the loop, which make_splash.sh uses for the stills
#
# The ride is scripted with console commands on a fixed 30 fps timestep (-benchmark -fps=30), so
# every run films the same ride. kitesurf.Shot cuts between cinematic cameras and
# kitesurf.CaptureFrames writes each frame to Saved/MenuVideo/loop/ and intro/. Needs the GPU, ffmpeg (with
# libvpx-vp9) and ImageMagick 7.
#
#   scripts/render-menu-video.sh                 # 1920x1080, encode both videos
#   RES=960x540 scripts/render-menu-video.sh     # a quick look; writes into Saved/MenuVideo only
#   SKIP_CAPTURE=1 scripts/render-menu-video.sh  # re-encode the last take
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/common.sh"

RES="${RES:-1920x1080}"
RES_X="${RES%x*}"
RES_Y="${RES#*x}"
FPS=30
OUT_DIR="$PROJECT_ROOT/Saved/MenuVideo"
MOVIES_DIR="$PROJECT_ROOT/Content/Movies"
# Only a full-size take replaces the videos in Content.
if [[ "$RES" != "1920x1080" ]]; then
    MOVIES_DIR="$OUT_DIR"
fi

# Frames are counted from the start of the run. Filming starts once the rider is up and planing and
# the shaders are compiled. The ride is the same every run, so the intro is a second run of it with
# the camera circling the rider through the big jump.
LOOP_START=150
LOOP_FRAMES=750
INTRO_START=385
INTRO_FRAMES=180
CROSSFADE_FRAMES=30

# frame-from-start  command
RIDE=(
    "100 kitesurf.HideUI"
    # Ride with the kite held low on its side of the window; left alone it climbs to the zenith
    # and the rider stops.
    "20 kitesurf.HoldKite 50"
    # A windy day on a small kite, as the big jump tests ride it.
    "10 kitesurf.Wind 30"
    "11 kitesurf.Kite 0"
    # The send and pop as the physics tests fly it (RunJump in RideLoopTests.cpp): bar towards
    # the kite, weight on the tail and the jump button held to load the crouch...
    "400 kitesurf.HoldKite off"
    "400 kitesurf.Input -1 0 0 -1 0"
    "400 kitesurf.Load 1"
    # ...then after about 0.8 s pull (later goes higher but lands hot) the bar in and let go of the button: the pop.
    "424 kitesurf.Input 0 1 0 -1 0"
    "425 kitesurf.Load 0"
    "430 kitesurf.Input 0 1 0 0 0"
    # Bar centred in the air: the assist holds the kite overhead. Then ride on with the kite low.
    "445 kitesurf.Input 0 0 0 0 0"
    "560 kitesurf.HoldKite 50"
    # Loop the kite.
    "680 kitesurf.HoldKite off"
    "680 kitesurf.Input 1 0 0 0 0"
    "770 kitesurf.Input 0 0 0 0 0"
    "770 kitesurf.HoldKite 50"
)
LOOP_SHOTS=(
    "$LOOP_START kitesurf.CaptureFrames $LOOP_FRAMES loop"
    # Riding into a long lens planted ahead.
    "$LOOP_START kitesurf.Shot Wide"
    # Low over the water as the rider rushes past.
    "270 kitesurf.Shot Low"
    # Side on for the send and the pop...
    "390 kitesurf.Shot Side"
    # ...and round the rider while they hang in the air.
    "450 kitesurf.Shot Orbit"
    # Down the lines from behind the kite.
    "570 kitesurf.Shot KiteView"
    # Behind the rider for the kite loop.
    "660 kitesurf.Shot Chase"
    "780 kitesurf.Shot Side"
)
INTRO_SHOTS=(
    "$INTRO_START kitesurf.CaptureFrames $INTRO_FRAMES intro"
    "$INTRO_START kitesurf.Shot Orbit"
)

# Plays the ride with the given shots in the game, offscreen, on a fixed timestep.
film() {
    local cmds="" entry frame
    # The ride's state in the log once a second, to see what the script did.
    for ((frame = 30; frame < 1200; frame += 30)); do
        cmds+="kitesurf.After $frame kitesurf.State,"
    done
    for entry in "${RIDE[@]}" "$@"; do
        cmds+="kitesurf.After ${entry},"
    done
    __NV_PRIME_RENDER_OFFLOAD=1 __GLX_VENDOR_LIBRARY_NAME=nvidia "$UE_EDITOR" "$UPROJECT" /Game/Maps/L_OpenWater \
        -vulkan -game -RenderOffScreen -ResX="$RES_X" -ResY="$RES_Y" -ForceRes -benchmark -fps=$FPS \
        -unattended -nosound -log -ExecCmds="${cmds%,}"
}

# Fails unless a take has at least the expected number of frames.
check_take() {
    local count
    count=$(find "$OUT_DIR/$1" -name 'frame_*.png' | wc -l)
    if (( count < $2 )); then
        echo "ERROR: expected $2 frames in $OUT_DIR/$1, found $count" >&2
        exit 1
    fi
}

# Films a take, and once more if it comes back short: a run started straight after another can
# fail to get GPU memory at its first frame (Vulkan "Out Of Memory" in the log) before the last
# one has let go of it.
film_take() {
    local name="$1" frames="$2"
    shift 2
    local attempt
    for attempt in 1 2; do
        rm -rf "${OUT_DIR:?}/$name"
        sleep 5
        film "$@" || true
        if (( $(find "$OUT_DIR/$name" -name 'frame_*.png' 2>/dev/null | wc -l) >= frames )); then
            return 0
        fi
        echo "The $name take came back short (attempt $attempt)" >&2
    done
}

if [[ -z "${SKIP_CAPTURE:-}" ]]; then
    echo "=== Filming the loop: $LOOP_FRAMES frames at $RES ==="
    film_take loop $LOOP_FRAMES "${LOOP_SHOTS[@]}"
    echo "=== Filming the intro: $INTRO_FRAMES frames at $RES ==="
    film_take intro $INTRO_FRAMES "${INTRO_SHOTS[@]}"
fi
check_take loop $LOOP_FRAMES
check_take intro $INTRO_FRAMES
# Without -ForceRes an offscreen window is quietly made smaller than asked for.
SIZE=$(magick identify -format '%wx%h' "$OUT_DIR/loop/frame_00000.png")
if [[ "$SIZE" != "$RES" ]]; then
    echo "ERROR: frames are $SIZE, expected $RES" >&2
    exit 1
fi
mkdir -p "$MOVIES_DIR"

# VP9 in WebM: the engine's Electra player decodes it on every desktop platform, Linux included.
VP9=(-c:v libvpx-vp9 -pix_fmt yuv420p -b:v 0 -crf 34 -row-mt 1 -deadline good -cpu-used 2 -an)

echo "=== Encoding the menu loop ==="
# Seamless: the last CROSSFADE_FRAMES dissolve into the first ones, and the loop starts after them.
seconds() { awk -v f="$1" -v fps=$FPS 'BEGIN { printf "%.4f", f / fps }'; }
FADE_SECONDS=$(seconds $CROSSFADE_FRAMES)
FADE_START=$(seconds $((LOOP_FRAMES - 2 * CROSSFADE_FRAMES)))
ffmpeg -y -hide_banner -loglevel warning -framerate $FPS -i "$OUT_DIR/loop/frame_%05d.png" -filter_complex "
    [0:v]trim=start_frame=0:end_frame=$LOOP_FRAMES,setpts=PTS-STARTPTS,split[a][b];
    [a]trim=start_frame=$CROSSFADE_FRAMES,setpts=PTS-STARTPTS[body];
    [b]trim=start_frame=0:end_frame=$CROSSFADE_FRAMES,setpts=PTS-STARTPTS[head];
    [body][head]xfade=transition=fade:duration=$FADE_SECONDS:offset=$FADE_START,format=yuv420p[out]" \
    -map '[out]' "${VP9[@]}" "$MOVIES_DIR/MenuLoop.webm"

echo "=== Drawing the title and encoding the intro ==="
"$SCRIPT_DIR/editor/make_splash.sh" --title-only "$OUT_DIR/title.png" "$RES_X" "$RES_Y"
# The shot fades up from black, the title slides up and fades in, and the end fades to white for the
# cut to the menu.
INTRO_SECONDS=$(seconds $INTRO_FRAMES)
ffmpeg -y -hide_banner -loglevel warning -framerate $FPS -i "$OUT_DIR/intro/frame_%05d.png" \
    -loop 1 -framerate $FPS -i "$OUT_DIR/title.png" -filter_complex "
    [0:v]trim=end_frame=$INTRO_FRAMES,setpts=PTS-STARTPTS,fade=t=in:st=0:d=0.8[shot];
    [1:v]format=rgba,trim=end_frame=$INTRO_FRAMES,setpts=PTS-STARTPTS,fade=t=in:st=1.2:d=1.0:alpha=1[title];
    [shot][title]overlay=x=0:y='max(0,(1-(t-1.2)/1.0))*H*0.06':shortest=1,
        fade=t=out:st=$(seconds $((INTRO_FRAMES - 18))):d=0.6:color=white,format=yuv420p[out]" \
    -map '[out]' "${VP9[@]}" "$MOVIES_DIR/Intro.webm"

# A frame from the jump for the splash and the still menu background.
cp "$OUT_DIR/intro/frame_$(printf %05d 100).png" "$OUT_DIR/keyframe.png"

ls -la "$MOVIES_DIR"/MenuLoop.webm "$MOVIES_DIR"/Intro.webm
echo "Loop $(seconds $((LOOP_FRAMES - CROSSFADE_FRAMES)))s, intro ${INTRO_SECONDS}s"
