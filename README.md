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

The bar (the kite) is on the arrow keys or the right stick; the board is on WASD or the left stick. The left stick and WASD are read by what the rider is doing: the board on the water, the pre-wind while the jump button and the rotation modifier (Shift / LT) are both held, the rotation in the air while the modifier is held. Without the modifier the stick is always the board, so a plain jump stays straight. The motion bar and the mouse bar only fly the kite, so the stick's rotation works alongside them.

| Action | Keyboard / mouse | Gamepad |
| --- | --- | --- |
| Steer the kite round the window (over the top to change tack) | Left / Right | Right stick left / right |
| Bar in / out: power (unhooked: your arms in / out). Let go and the bar springs back to the middle (Settings: BAR TO MIDDLE; off, it holds its position) | Down / Up (hold) | Right stick pulled back / pushed forward, RT |
| Loop the kite | Keep steering towards the kite's own side | Keep the stick towards the kite's own side |
| Bar on the mouse (the bar stays where the mouse leaves it) | Hold right button: move to steer and sheet | |
| Motion bar: recentre, with the bar in the middle | Home | Right stick click |
| Turn the board left / right | A / D | Left stick left / right |
| Weight on the nose / the tail of the board | W / S | Left stick up / down |
| Hold to crouch and load the edge; let go to pop | Space | Bottom face button |
| Rotation modifier: hold to let the stick reach the pre-wind (loading) or the rotation (in the air); without it the stick is always the board | Left Shift | LT |
| Pre-wind a rotation: hold Shift / LT and a direction while loading | WASD + hold Space + Shift | Left stick + hold the bottom face button + LT |
| In the air, with Shift / LT held: towards the side of the screen your back is on is a back roll, away is a front roll; the up-diagonal is a spin; S / pulled back is a backflip | A / D / W / S + Shift | Left stick + LT |
| Hold jump in the air: tuck (spins faster) | Space | Bottom face button |
| In the air: grab the board with the front / back hand (hold) | Q / E | LB / RB |
| While grabbing: pick the zone (nose, toe edge, heel edge, tail) | W / S: nose / tail; A / D towards your chest: toe edge, your back: heel edge | Left stick, same directions |
| In the air: back foot out of the strap (hold; back in before landing) | C | Left stick click |
| On the water: unhook / hook back in. Unhooked the kite parks low at 45 deg, its power is fixed, and the bar moves your arms (in to the hips, out along the lines) | F | Top face button (Y) |
| Unhooked, in the air: pass the bar behind your back (with the lines slack and your back to the kite) | X | Left face button (X) |
| Reset | R | Right face button |
| Pause menu (resume, restart, gear, session, school, freestyle heat, settings, main menu, quit) | Esc or P | Start |
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
gets there if you do not want one. In the air a full bar held for a moment (0.3 s; a quick tap
still flies the kite across) loops the kite from wherever it is, either way, and a full bar the other way mid-loop loops it back (half one way and half the other is an
S-loop; a loop against your direction of travel is a contra loop). Less than a full bar (the stick
or the mouse part way) still flies it across, and a send held from the water does not loop until
you ease it.

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
motion sensors are the bar: hold the controller like a bar and tilt it to steer (35 degrees is full
steering). MOTION POWER picks how it reads power: TILT (the default) is the pad tipped towards you;
MOVE is the pad moved down or towards you, as a real bar is pulled in, and up or away to let it out
(about 25 cm of travel is the whole throw; keep strokes brisk, under a second, because a pad cannot
tell a slow movement from holding still, and recentre when the bar has wandered). However you are
holding it when the ride starts is level; the right stick click (Home) recentres with the bar in the
middle, and reset (R) re-centres where it is. The triggers and bar keys trim the bar on top of the
controller while held. It needs a controller with motion sensors (PlayStation DualSense or
DualShock 4, Switch Pro); Xbox controllers have none, and with no sensors found the right stick
carries on working. Linux only for now.

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

**Best-three session.** BEST-THREE SESSION (90 s) in the pause menu (or `kitesurf.Session
[seconds]` in the console) starts a timed session: the best three jumps count, one per trick, and
a repeat is paid less (75%, 50%, ...). A jump in the air at the horn still counts if it took off
before it. The clock and the counting scores show at the top; at the end a results card shows the
total, the three jumps and your local best for that length, which is saved with the settings.

**Freestyle heat.** FREESTYLE HEAT (7 tricks) in the pause menu (or `kitesurf.Heat freestyle
[attempts] [countdown s]` in the console) starts a GKA-style heat of seven attempts. An attempt is an
unhooked jump of more than 0.4 s in the air, or any crash, which scores 0; hooked jumps do not count
("Unhook for freestyle"). Each trick scores 0.1 to 10 from its difficulty, landing and height; only
the best trick in each family counts, four tricks count (at most two heelside, three variety), and
one to four families earn a variety bonus of 1, 2, 4 or 7. Each attempt has 90 s (`kitesurf.Heat
freestyle 7 0` turns that off), or it is lost. The top row shows "Trick 3/7", the countdown and the
total with the bonus, a list at the right the counting tricks with their families, and at the end a
results card the total, the tricks, the families, the bonus and your local best for that number of
attempts, saved with the settings. A heat does not start during a session or a lesson.

**Kite school.** SCHOOL in the main menu, or in the pause menu during a ride, opens the lesson
menu: six chapters of lessons, each tile with its stars (0 to 3) and whether it is new, locked
behind a lesson you have not passed, or coming soon (the feature it needs is not in the game yet).
Pick a lesson to see what it teaches, what it needs, what passes it and your best result, then
START it, with more wind or fewer assists on a rerun for more stars. CONTINUE starts the lesson
the game recommends next. RESET PROGRESS forgets the lessons (it asks first, and keeps your trick
book). During a lesson the pause menu offers RETRY LESSON, LESSON MENU and FREE RIDE.

**First run.** The tutorial is the kite school's first three lessons. The first time you press
PLAY (until you finish or skip the tutorial) the game starts lesson A1, the kite power dive, on
flat water instead of opening the gear screen; the result card's Next goes on to A2 (water start)
and A3 (speed control). A line under the lesson panel welcomes you and says how to skip: in the
pause menu FREE RIDE reads SKIP TUTORIAL, which ends the lesson in free ride and stops PLAY
starting it again. Passing A3 finishes the tutorial, and its result card points on to the school
(Next goes to A4; jumps start at B1, or B2 if you already have B1) or free ride. The lessons stay
in the School menu for reruns.

**Gear.** PLAY opens the gear screen (after the first-run tutorial), and GEAR in the pause menu opens it during a ride.
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
