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
#   scripts/render-menu-video.sh                 # 3840x2160 (4K), encode both videos
#   RES=960x540 scripts/render-menu-video.sh     # a quick look; writes into Saved/MenuVideo only
#   SKIP_CAPTURE=1 scripts/render-menu-video.sh  # re-encode the last take
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/common.sh"

FULL_RES=3840x2160
RES="${RES:-$FULL_RES}"
RES_X="${RES%x*}"
RES_Y="${RES#*x}"
FPS=30
OUT_DIR="$PROJECT_ROOT/Saved/MenuVideo"
MOVIES_DIR="$PROJECT_ROOT/Content/Movies"
# Only a full-size take replaces the videos in Content.
if [[ "$RES" != "$FULL_RES" ]]; then
    MOVIES_DIR="$OUT_DIR"
fi

# The loop is cut from short clips, each filmed in its own run of the same ride from the same start,
# so every trick is flown from the same steady ride rather than from wherever the last one left the
# rider. Frames are counted from the start of a run; filming starts once the rider is up and
# planing and the shaders are compiled. The intro is one more clip.
FADE_FRAMES=30
LOOP_CLIPS=(cruise send looptransition airloop heliloop)

# Every run: no UI, a windy day on a small kite (as the big jump tests ride it), and the kite held
# low on its side of the window: left alone it climbs to the zenith and the rider stops.
RIDE=(
    "10 kitesurf.Wind 30"
    "11 kitesurf.Kite 0"
    "20 kitesurf.HoldKite 50"
    "100 kitesurf.HideUI"
)

# The send and pop as the physics tests fly it (RunJump in RideLoopTests.cpp), from frame 400: bar
# towards the kite, weight on the tail and the jump button held to load the crouch; after 0.8 s
# pull the bar in and let go of the button (later goes higher but lands hot). In the air with the
# bar centred the assist holds the kite overhead. 5.3 m, 3.9 s, landing at about frame 542.
SEND=(
    "400 kitesurf.HoldKite off"
    "400 kitesurf.Input -1 0 0 -1 0"
    "400 kitesurf.Load 1"
    "424 kitesurf.Input 0 1 0 -1 0"
    "425 kitesurf.Load 0"
    "430 kitesurf.Input 0 1 0 0 0"
    "445 kitesurf.Input 0 0 0 0 0"
)

# Each clip sets CLIP_START and CLIP_FRAMES (the frames filmed) and CLIP (its inputs and shots).
clip_cruise() {
    CLIP_START=150 CLIP_FRAMES=240
    CLIP=(
        # Riding into a long lens planted ahead, then low over the water as the rider rushes past.
        "150 kitesurf.Shot Wide"
        "270 kitesurf.Shot Low"
    )
}
clip_send() {
    CLIP_START=390 CLIP_FRAMES=180
    CLIP=("${SEND[@]}"
        # Side on for the send and pop, then round the rider while they hang in the air.
        "390 kitesurf.Shot Side"
        "450 kitesurf.Shot Orbit"
    )
}
clip_looptransition() {
    CLIP_START=415 CLIP_FRAMES=150
    CLIP=(
        # A kite loop on the water that turns the rider onto the other tack: the kite lifted high
        # first (a loop from low puts it in the water), then the bar held towards its own side
        # loops it down through the power zone while the rider carves round.
        "415 kitesurf.Shot Orbit"
        "380 kitesurf.HoldKite 15"
        "430 kitesurf.HoldKite off"
        "430 kitesurf.Input 1 0 0 0 1"
        "475 kitesurf.Input 1 0 -1 0 1"
        "500 kitesurf.Input 0 0 -1 0 0"
        "520 kitesurf.HoldKite -45"
        "520 kitesurf.Input 0 0 0 0 0"
    )
}
clip_airloop() {
    CLIP_START=390 CLIP_FRAMES=180
    CLIP=("${SEND[@]}"
        # The big jump with a kite loop from the apex: the bar held over at the top, and the kite goes
        # round as the rider comes down (276 deg by touchdown, then the rest). A loop started earlier,
        # to finish high, stops holding the rider up and they crash: the model's airborne loop
        # (KiteSurf.Physics.AirborneLoopYanks) does not yet lift.
        "390 kitesurf.Shot Side"
        "455 kitesurf.Input -1 0 0 0 1"
        "530 kitesurf.Input 0 0 0 0 0"
    )
}
clip_heliloop() {
    CLIP_START=440 CLIP_FRAMES=150
    CLIP=("${SEND[@]}"
        # A heli loop: a downloop from the top of the jump on the way down, going round (345 deg)
        # through the landing. Started later than this it stalls once the rider is down.
        "440 kitesurf.Shot Low"
        "470 kitesurf.Input -1 0 0 0 1"
        "560 kitesurf.Input 0 0 0 0 0"
    )
}
clip_intro() {
    # Circling the rider through the big jump, under the title.
    CLIP_START=385 CLIP_FRAMES=180
    CLIP=("${SEND[@]}" "385 kitesurf.Shot Orbit")
}

# Plays the ride with a clip's inputs and shots in the game, offscreen, on a fixed timestep, holding the GPU
# lock (common.sh) for the take.
film() {
    local cmds="" entry frame
    # The ride's state in the log once a second, to see what the script did.
    for ((frame = 30; frame < 1200; frame += ${STATE_EVERY:-30})); do
        cmds+="kitesurf.After $frame kitesurf.State,"
    done
    for entry in "${RIDE[@]}" "$@"; do
        cmds+="kitesurf.After ${entry},"
    done
    __NV_PRIME_RENDER_OFFLOAD=1 __GLX_VENDOR_LIBRARY_NAME=nvidia with_gpu_lock "$UE_EDITOR" "$UPROJECT" /Game/Maps/L_OpenWater \
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

# Films a take, and once more if it comes back short: a run can still fail to get GPU memory at
# its first frame (Vulkan "Out Of Memory" in the log), e.g. next to a GPU program that does not
# take the lock.
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

# CLIPS films only some, e.g. CLIPS="airloop" while working one out; the videos are encoded only
# when every clip has been filmed.
CLIPS=(${CLIPS:-${LOOP_CLIPS[*]} intro})
LOOP_FRAMES=0
for name in "${LOOP_CLIPS[@]}" intro; do
    "clip_$name"
    if [[ " ${CLIPS[*]} " == *" $name "* && -z "${SKIP_CAPTURE:-}" ]]; then
        echo "=== Filming $name: $CLIP_FRAMES frames at $RES ==="
        film_take "$name" "$CLIP_FRAMES" "${CLIP[@]}" "$CLIP_START kitesurf.CaptureFrames $CLIP_FRAMES $name"
    fi
    [[ "$name" == intro ]] && INTRO_FRAMES=$CLIP_FRAMES || LOOP_FRAMES=$((LOOP_FRAMES + CLIP_FRAMES))
done
for name in "${LOOP_CLIPS[@]}" intro; do
    if (( $(find "$OUT_DIR/$name" -name 'frame_*.png' 2>/dev/null | wc -l) == 0 )); then
        echo "Filmed ${CLIPS[*]}; the videos need every clip, so not encoding."
        exit 0
    fi
done
for name in "${LOOP_CLIPS[@]}"; do "clip_$name"; check_take "$name" "$CLIP_FRAMES"; done
clip_intro; check_take intro "$CLIP_FRAMES"
# Without -ForceRes an offscreen window is quietly made smaller than asked for.
SIZE=$(magick identify -format "%wx%h" "$OUT_DIR/${LOOP_CLIPS[0]}/frame_00000.png")
if [[ "$SIZE" != "$RES" ]]; then
    echo "ERROR: frames are $SIZE, expected $RES" >&2
    exit 1
fi
mkdir -p "$MOVIES_DIR"

# VP9 in WebM: the engine's Electra player decodes it on every desktop platform, Linux included.
# Tiles let the player decode a 4K frame on several cores at once.
VP9=(-c:v libvpx-vp9 -pix_fmt yuv420p -b:v 0 -crf 34 -row-mt 1 -tile-columns 3 -frame-parallel 0 -deadline good -cpu-used 2 -an)

echo "=== Encoding the menu loop ==="
# The clips one after another with hard cuts; the last FADE_FRAMES dissolve into the first ones,
# and the loop starts after them, so it is seamless.
seconds() { awk -v f="$1" -v fps=$FPS 'BEGIN { printf "%.4f", f / fps }'; }
INPUTS=() CONCAT=""
for i in "${!LOOP_CLIPS[@]}"; do
    INPUTS+=(-framerate $FPS -i "$OUT_DIR/${LOOP_CLIPS[$i]}/frame_%05d.png")
    "clip_${LOOP_CLIPS[$i]}"
    CONCAT+="[$i:v]trim=end_frame=$CLIP_FRAMES,setpts=PTS-STARTPTS[c$i];"
done
for i in "${!LOOP_CLIPS[@]}"; do CONCAT+="[c$i]"; done
ffmpeg -y -hide_banner -loglevel warning "${INPUTS[@]}" -filter_complex "
    ${CONCAT}concat=n=${#LOOP_CLIPS[@]}:v=1:a=0,split[a][b];
    [a]trim=start_frame=$FADE_FRAMES,setpts=PTS-STARTPTS[body];
    [b]trim=start_frame=0:end_frame=$FADE_FRAMES,setpts=PTS-STARTPTS[head];
    [body][head]xfade=transition=fade:duration=$(seconds $FADE_FRAMES):offset=$(seconds $((LOOP_FRAMES - 2 * FADE_FRAMES))),format=yuv420p[out]" \
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
echo "Loop $(seconds $((LOOP_FRAMES - FADE_FRAMES)))s, intro ${INTRO_SECONDS}s"
