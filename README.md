# KiteSurf

Kitesurfing game built on Unreal Engine 5.8 with C++ and Enhanced Input.

## Repository Layout

- `Source/KiteSurf/` — Core game module (Runtime)
  - `Public/` & `Private/` — C++ classes: `AKiteRiderPawn`, `UWindComponent`, `AKiteSurfGameMode`
  - `Private/Tests/` — Automation tests (`KiteSurf.Wind.ReturnsBaseWind`, `KiteSurf.Pawn.ClampsInputs`)
- `Config/` — Engine & Input configurations (`DefaultEngine.ini`, `DefaultInput.ini`)
- `Content/` — Unreal assets (maps, meshes, audio)
- `scripts/` — Project workflow scripts:
  - `common.sh` — Engine path resolution and environment variables
  - `build.sh` — Compiles the editor target
  - `run-tests.sh` — Executes automation tests
  - `run-editor.sh` — Launches Unreal Editor with Vulkan + NVIDIA offload
  - `package-linux.sh` — Packages Linux Shipping build via RunUAT
  - `free-resources.sh` — Stop/start host k3s service to free RAM/GPU
  - `setup-engine.sh` — Installs Linux runtime dependencies and extracts engine
- `docs/` — Architecture specifications and task roadmap

## Getting Started

1. **Free system resources** (if host is running background services):
   ```bash
   scripts/free-resources.sh stop
   ```

2. **Install Unreal Engine 5.8** (one-time setup):
   ```bash
   scripts/setup-engine.sh /path/to/Linux_Unreal_Engine_5.7.x.zip /opt/unreal-engine
   ```

3. **Build the project**:
   ```bash
   scripts/build.sh
   ```

4. **Run automation tests**:
   ```bash
   scripts/run-tests.sh -nullrhi
   ```

5. **Launch the editor**:
   ```bash
   scripts/run-editor.sh
   ```

## Playing

```bash
scripts/run-editor.sh -game -windowed -ResX=1600 -ResY=900 -log
```

You start planing across the wind with the kite parked low on your right. The bar stays where
you leave it, so no key needs to be held to keep riding; with the bar centred the kite slowly
climbs the edge of the window towards 12 o'clock, as a real one does. The same table is shown in
the main menu and the pause menu.

The bar (the kite) is on the arrow keys or the right stick; the board is on WASD or the left stick.

| Action | Keyboard / mouse | Gamepad |
| --- | --- | --- |
| Steer the kite round the window (over the top to change tack) | Left / Right | Right stick left / right |
| Bar in / out: power (the bar holds its position) | Down / Up | Right stick pulled back / pushed forward, triggers |
| Loop the kite | Keep steering towards the kite's own side | Keep the stick towards the kite's own side |
| Bar on the mouse | Hold right button: move to steer and sheet | |
| Turn the board left / right; spin it in the air | A / D | Left stick left / right |
| Weight on the nose / the tail of the board | W / S | Left stick up / down |
| Hold to crouch and load the edge; let go to pop | Space | Bottom face button |
| Reset | R | Right face button |
| Pause menu (resume, restart, gear, settings, main menu, quit) | Esc or P | Start |
| In menus: move up and down, change a value, select | Up / Down, Left / Right, Enter or Space; Esc goes back | D-pad or left stick, bottom face button; right face button goes back |

**The spot.** The gear screen also switches what is in the water. Sandbars lie across your
reach, about 260 m out on the first run: jump them, because riding onto sand is a crash.
Islands with palms sit further off to ride round. Sharks patrol in circles; they leave a rider
who is up and riding alone unless you run one over, and come for you when you are down in the
water. Anything you are above, you clear.

**Turning.** A / D carve the board, and you lean into the turn. Turned up away from the kite, the
board slows and stalls once it points past square to the lines, and it can never point further
from their pull than your body twists against the harness hook on your front: past that, holding
the turn leans you back against the hook and the board stops turning, so it does not carry you
round under the kite. Let go and the board comes back towards the kite and the pull gets you going
again. Turning towards the kite is not limited.

**Steering and loops.** The bar works like a real one, with no extra key. Steer away from the
side the kite is on and it flies up over the top to the other side; let go and it drifts up the
edge of the window to 12 o'clock overhead and sits there. In the air, with the bar centred, the
kite is flown to 12 over you and held there, so it carries you down. Keep the bar held towards the
side the kite is already on and it turns down and round: a loop, for as long as you hold it. So
holding the bar through a change of direction ends in a loop on the new side; let go as the kite
gets there if you do not want one.

**Jumping.** You can always pop while you are up on the board: tap the jump button for a hop
of about a metre. The height comes from the kite. Hold the jump button to crouch with your weight
back and load the edge: the board heels harder against the lines, the pull builds, and you are
held down against the kite as it rises. Steer the kite up hard, pull the bar in, and let go of
the button as the pull builds to pop. Let go too early and the kite has not loaded up yet; hold
on too long and it rips you off your edge, which is a much lower jump. Timed well, that is about
4.5 m in 15 kn, 11 m in 30 kn and 12 to 15 m in 40 kn, and with the bar centred the kite flown
overhead carries you down: 5 s in the air at 30 kn. In a 90 kn hurricane on a 2 m kite it is about
22 m up and 225 m downwind, and it only just lands, crouched, at 9.9 g. The HUD shows the
height and distance of a jump as it happens and when it ends. On the way down, hold the jump button
again to crouch for the landing. Steering the kite up without an edge just plucks you off the water.
(Weight back on S adds to the pop.) A kite looped through the middle of the window pulls several
times harder than a parked one.

**Landing.** A landing's load is shown in g as you touch down (LANDED 4.2 g), from how fast you
were sinking and how far your legs and the board took it out over; crouched, that distance is
twice as long. It says HOT if you came down fast (over 6 m/s) or with the kite low (under 45 degrees
up), and past 10 g it is a crash, as is landing with the board across your course. The big 30 kn
jump lands hot at 4.8 g crouched and 8.6 g standing; the biggest 40 kn jump (15 m) lands hot at
6.9 g crouched and is a crash standing; the storm jumps let go early land crouched at 9.4 g (60 kn)
and 9.9 g (90 kn), and the higher ones, the best 60 kn jump included (11.2 g), crash even crouched.
The card that names and scores the jump shows the same g.

**Motion bar.** Settings has a MOTION BAR switch (off by default). With it on, a controller's
motion sensors are the bar: hold the controller like a bar, tilt it to steer (35 degrees is full
steering), and pull it in towards you like a bar for power. However you are holding it when the
ride starts is level; reset (R) re-centres. The right stick, triggers and bar keys then leave the
bar alone. It needs a controller with motion sensors (PlayStation DualSense or DualShock 4,
Switch Pro); Xbox controllers have none, and with no sensors found the right stick carries on
working. Linux only for now.

**Vibration.** The controller buzzes briefly on the pop, on landing (harder for a harder
landing), on a crash, when the kite hits the water, and once when the lines yank hard, as in a
loop. Settings has a VIBRATION switch (on by default).

**Sound and music.** The ride has wind, water, spray, line, kite and canopy sounds that follow
what the rider is doing, and music with a second layer that comes in while you are in the air.
Settings has separate MASTER, MUSIC, AMBIENT (wind, water, lines, kite) and EFFECTS (pop,
landings, crashes, menu sounds) volume sliders. Everything is synthesised by scripts in
`scripts/editor/`; see the Sound section of `docs/movement.md`.

**Power.** The bar is the throttle, and it moves through its whole throw in under half a
second. Right out, the kite flags and barely pulls (about 160 N on the 9 m in 20 kn, 6 kn of board
speed); right in it pulls five times as hard (850 N, 21 kn). The ride starts with it 70% in. In
light wind ride with it right in: the 12 m only planes in 12 kn that way.

**Gear.** PLAY opens the gear screen, and GEAR in the pause menu opens it during a ride.
Pick the wind (8 to 90 kn: a light breeze to a hurricane), then rig for it:

- *Kite size*: 9 m to start with, which suits the default 20 kn. Choose from 2 to 17 m, or AUTO
  for what a rider would rig for the wind (12 m in 15 kn, 6 m in 30 kn, 2 m in 90 kn). Small kites turn and loop faster; big ones pull harder and are a
  handful when it blows.
- *Kite*: the 3-strut loop kite turns tight and fast; the 5-strut boost kite has more lift and
  glide for height and hangtime, and turns slower.
- *Board*: the 132 pops harder and turns quicker but needs more speed to plane and sinks sooner
  in light wind; the 145 planes early and grips, with less pop; the 138 is the all-rounder.
- *Rider*: who is on the board.

Without enough speed the board does not carry you: you float chest-deep, slow through the water
at any speed, until the kite's pull lifts you onto the board and it planes (bar in and dive the
kite to water start). After a crash you are put back on the board at 8 kn, however hard the kite
was pulling while you were down. The BAR panel at the bottom right shows what your hands are
doing: the bar slides down as you pull it in and tilts as you steer, with a marker for the
steering that actually reaches the kite.

**Reading the wind.** White streaks on the water lie along the wind and drift down it. The WIND
dial at the top right is a flag seen from above, with the top of the dial the way you are
looking: the flag streams the way the wind blows, and the line under it says where the wind is
coming from ("from the right", "from behind left").

The wind gusts and drops (the HUD calls out GUST and LULL). The kite only pulls while its lines
are tight: in a hole in the wind, or if you outrun the wind, the lines go slack and the kite
falls until they come tight again. Pull the bar in too far at low speed and it stalls; let it out.

Loop it too low and it goes into the water, where it lies with slack lines until it relaunches
(about three seconds, or sooner if you steer). Weight on the tail (S) digs the rail in and loads
the pop; weight on the nose (W) flattens the board so it runs faster and slides more.

The rider is chosen in Settings: Santa (the default), a wetsuit rider, or the robot.

## Conventions

- **Units**: Unreal cm, cm/s (1 knot = 51.44 cm/s).
- **Water Plane**: World Z = 0.
- **Rendering**: Vulkan SM6, Lumen, Nanite, VSM, TSR.
- **Asset storage**: Large binary files (`*.uasset`, `*.umap`, etc.) are tracked with Git LFS.
