# Tricks: research and implementation plan

Written on 2026-10-02 against `main` at `4aa65f8`, and refreshed on 2026-10-03. The physics
rework (#45, the fixed step) is now on `main`, and the first trick modules have merged. What is
done, and what each PR verified, is in the status table in `docs/tricks/README.md`. Sections
3 to 6 are the research and design. Sections 5 and 7 describe the code before the trick work
started, so read them together with that table.

`docs/research.md` already covers big air basics: the jump phases, kite loop physics, the base
big air trick list, King of the Air and GKA big air judging, and backlog epics C to F. This
document adds three things and does not repeat that material:

1. A wider trick catalogue: the big air tricks missing from `research.md`, the full freestyle
   (unhooked) family, grabs, board-offs and strapless.
2. What other action sports games do for trick input, recognition and scoring.
3. A concrete plan for this codebase: data model, physics, input, rig, recognition, scoring,
   HUD and tests, ordered into milestones that fit around the physics rework.

Confidence tags as in `research.md`: **sourced**, **typical** (coaching text), **derived**
(from sourced numbers with ballistics), **estimate** (a tuning starting point, not a
requirement).

Code-level plans per milestone, and the decisions that reconcile them, are in `docs/tricks/`
(start with `docs/tricks/README.md`).

## 1. How the disciplines differ

| | Big air | Freestyle | Strapless |
| --- | --- | --- | --- |
| Where height comes from | The kite: sent back through 12 while the edge holds, then lift through the climb | The board: hard edge against a low kite, then pop and scoop. The kite is not sent | Either, smaller |
| Kite position | Overhead, or looped through the window | Parked low, about 45° (10 to 11 or 1 to 2 o'clock); dipped at the apex to slacken the lines for a pass | Parked or looped |
| Hooked in | Almost always; unhooked passes exist at the top level | Unhooked for nearly every scored trick | Both |
| Height and airtime | 5 to 35 m, 4 to 13 s | 1.8 ± 0.4 m, 1.3 ± 0.2 s (**sourced**, Simons 2025); 1 to 4 m in a game | 2 to 10 m |
| Rotation rate | Single back roll 1.5 to 2.5 s (150 to 250°/s) (**estimate**) | 360° in 0.5 to 0.9 s (400 to 700°/s) (**estimate**) | |
| Judged on | Height, extremity, technicality, execution | Height, technical difficulty, power, risk, smoothness, innovation; best score per trick family | One trick per category, first trick per tack |

Freestyle jumps are close to ballistic. `h = g t² / 8` at 1.3 s gives 2.1 m, which matches the
measured height (**derived**). WOO's freestyle mode scores no pop for any jump with more than
3 s of airtime ("floaty"), because by then the kite is lifting (**sourced**, WOO docs). For the
game, a freestyle trick comes from the same pop as today's `LoadPopBonus`, but with the kite
held low and the line force pulling on the hands, not the hook.

## 2. Body geometry and words

The rider frame used by the rest of this document and by the code:

- **Up:** feet to head.
- **Front:** where the chest faces. Riding heelside today, this is roughly towards the kite.
- **Side:** hip to hip, along the board, positive towards the nose.

Rotations:

- **Spin:** about Up.
- **Roll** (back roll, front roll): about an axis between Up and Front. A flat roll is close to
  a spin. A fully inverted roll is a cartwheel about Front; the wakeboard term for the heelside
  version is a "reverse cartwheel".
- **Flip:** about Side. The tantrum is a heelside backflip; the slim chance uses a front flip.
- **Raley:** not a rotation. The line pull on the hands swings the body out horizontal, with
  the board up behind at head height, then back under.

Directions, which must be pinned down with a reference clip before code depends on them (see
open questions):

- **Back roll:** the chest turns first towards the tail; the rider looks back over the rear
  shoulder.
- **Front roll:** the chest turns first towards the nose.
- **Backside (BS) spin or pass:** the same sense as a back roll, so the back faces the kite
  first. Back to blind is a back roll plus BS 180 that keeps turning the same way.
- **Frontside (FS) spin or pass:** the other sense.

Stance, at take-off and at landing:

- **Heelside:** normal riding, chest to the kite, bar in front.
- **Toeside:** FS 180 from heelside, bar still in front.
- **Blind:** BS 180 from heelside, back to the kite, bar held behind the back and not passed.
- **Switch:** the board is ridden the other way round. A twin-tip does this freely.

Blind and toeside both point the body 180° from heelside. They differ in which way the bar and
lines go round the body, and the trick state has to record that.

Handle passes:

- **Handle pass (HP):** an unhooked spin in which the bar goes behind the back from one hand to
  the other.
- **Air pass:** completed in the air.
- **Surface pass:** completed on or just after touchdown.
- **Timing:** the pass is made at or just after the apex, while the lines are slack. The rider
  makes that slack by dipping the kite. The window is about 0.2 to 0.4 s of a 1.3 s jump
  (**typical** for the timing, **estimate** for the length).

## 3. Trick catalogue

### 3.1 Big air tricks not in `research.md`

`research.md` already lists the sent jump, grab, back and front roll, heli loop landing,
one-footer, board-off, deadman, kite loop, megaloop, megaloop back roll and board-off, boogie,
contra, doobie, double and triple loops, and the S-loop. From the GKA big air list (2023 and the
2025 rulebook appendix C):

| Trick | What happens | Difficulty | Prerequisite |
| --- | --- | --- | --- |
| Double, triple back roll | Two or three rolls in one jump | 3, 4 | Back roll |
| Late or early back roll in a loop | Early: the roll starts mid-loop and is nearly done when the yank comes, which is easier. Late: loop first, then roll from about 3/4 of the loop | 4, 5 | Megaloop back roll |
| One-foot roll or loop | One foot out of the strap during a roll or loop | 3 | One-footer |
| Board pass | Board taken off and passed round the back | 4 | Board-off |
| Tic tac | Board held by one rail and spun 360 back to the same rail | 4 | Board-off |
| Board flip | Board flipped over in the hands and put back | 4 | Board-off |
| Superman | Both feet out, body stretched behind the board, which is held by the rail or handle | 4 | Board-off |
| Deadman front or back roll | Deadman held through a roll | 4 | Deadman |
| Sent pass (1, 3, 5) | Unhooked sent jump with a handle pass | 4 | Freestyle passes |
| Kite loop FS or BS pass (3, 5, 7) | Unhooked spin with a pass during a kite loop | 5 | Kite loop, passes |
| Kite loop KGB, slim chance, front blind mobe, double half cab | Freestyle inversions and passes during a kite loop | 5 | Megaloop, the freestyle trick |
| Snake loop | S-loop plus one more loop | 5 | S-loop |
| Kung fu 1080 pass | Powered unhooked jump with a 1080 pass; King of the Air 2019 most extreme move. Exact definition uncertain | 5 | Sent pass |

"Hammerhead", "lobster", "rolling kiteloop" and "kitelooped dark slide" were searched for and
not found in any list. Leave them out.

### 3.2 Grabs

Names are inherited from skate and wakeboard (Elite Watersports, surfertoday). In the game each
is a hand plus a zone of the board:

| Hand | Nose | Toe edge, between the feet | Heel edge, between the feet | Tail | Through or behind the legs |
| --- | --- | --- | --- | --- | --- |
| Front | Nose | Mute | Melon | Seatbelt (across the body to the tail) | Chicken salad (heel), Thaipan (toe) |
| Back | Crail | Indy | Stalefish | Tail | Roast beef (heel), Canadian bacon (toe) |

Variations are named on top of these: method (melon with the board turned about 90°), Japan
(mute with the board pulled up behind), tindy and nuke. A big air grab is held for 1 to 3 s, "to
the very last second" (**typical**, MACkite). A freestyle grab takes 0.2 to 0.3 s to reach and is
held for 0.3 to 0.6 s (**estimate**).

### 3.3 Freestyle: the naming grammar

Verified against the Universkite trick dictionary, Wakeboarder.com and the GKA 2025 trick lists:

1. **The last number is the handle-pass spin in hundreds of degrees:** 1 = 180, 3 = 360,
   5 = 540, 7 = 720, 9 = 900, 10 = 1080. From 7 upwards there are two passes in one jump.
2. **"Frontside N" or "Backside N" on its own** is a flat pop (or raley) with that pass.
3. **The base name gives the take-off move and the pass direction:**

   | Base name | Take-off move | Pass |
   | --- | --- | --- |
   | Mobe | Back roll | FS |
   | KGB | Back roll | BS |
   | Back to blind | Back roll | BS 180 |
   | Slim chance | Front flip | FS |
   | Front blind (mobe) | Front flip | BS 180 (360) |
   | 313 | Raley | FS 360 |
   | Blind judge | Raley | BS 180, passed in the air |
   | S-mobe | S-bend | FS |
   | Heart attack | S-bend | BS |
   | Crow mobe | Toeside front roll | FS 360 |
   | Dum dum | Toeside front roll | BS 360 |

   Higher numbers follow on: 315, 317, 319 and 3110 are the 313 with 540 to 1080; KGB 5 and
   Slim 7 work the same way.
4. **"313" is a proper name, not a formula.** It is Shaun Murray's raley with a FS 360. "13" on
   its own means nothing, and "313 to blind" (GKA code 3132B, nicknamed "310") is a rewind.
   513, 713 and 715 are not established kite trick names.
5. **Prefixes:** toeside or blind give the take-off stance, double or 2x gives two inversions,
   half cab means a switch take-off plus 180. **Suffixes:** to blind or to wrapped give the
   landing state.
6. **Conflicting definitions:** the GKA and Universkite define the hinterberger as a raley with
   an overhead FS 360 and no pass. IKSURFMAG uses the name for a popped back roll with a FS 3.
   Use the GKA meaning.

### 3.4 Freestyle tricks

Difficulty is editorial (1 to 5). Every trick here is unhooked with the kite parked low, unless
noted otherwise.

| Trick | Body | Pass | Lands | Diff | Prerequisite |
| --- | --- | --- | --- | --- | --- |
| Unhooked pop | No rotation | | Heelside | 1 | Hooked pop |
| Surface pass | 360 on the water | Mid-spin, on the water | Heelside | 2 | Riding unhooked |
| Raley | Horizontal extension, swung back under | | Heelside, flat, nose to the kite | 2 | Unhooked pop |
| Raley to blind | Raley + BS 180 | Surface pass after landing | Blind | 2.5 | Raley |
| Krypt | Raley + FS 180 | | Toeside | 2.5 | Raley |
| Blind judge | Raley + BS 180 started at the apex | Air pass | Blind | 3 | Raley to blind |
| S-bend | Raley + overhead BS 360, off-axis | None (both hands over the head) | Heelside | 3 | Raley |
| Hinterberger | Raley + overhead FS 360 | None | Heelside | 3.5 | S-bend |
| Tantrum | Heelside backflip, back hand off at take-off | None | Heelside | 3 | Unhooked front roll |
| Back to blind | Inverted back roll + BS 180 | BS 180 | Blind | 3 | Unhooked back roll |
| 313 | Raley, then FS 360 | FS 360, front hand off at or just after the apex | Heelside | 3.5 | Raley to wrapped |
| Frontside or backside 3, 5, 7 | Flat pop + spin | Mid-rotation | Heelside for 3 and 7; blind or toeside for 5 (an odd number of half turns faces away from the kite) | 3 to 4.5 | Surface pass |
| KGB (5, 7) | Inverted back roll + BS 360 | At about 3/4 of the roll | Heelside for KGB and KGB 7; blind or toeside for KGB 5 | 4 | Back to blind |
| Back mobe (5, 7) | Inverted back roll, then FS 360 (720 total). Kite a little higher, about 75° | Just before the drop; the bar arrives before the board lands | Heelside | 4 | KGB or 313 |
| Crow mobe, dum dum | Toeside front roll + FS or BS 360 | | Heelside | 4 | Toeside riding |
| Slim chance (5, 7) | Front flip + FS 360, legs straightened skywards | Pass while inverted | Heelside, nose downwind | 4.5 | Front to blind, 313 |
| S-mobe, heart attack | S-bend + FS or BS 360 | | Heelside | 4.5 | S-bend |
| Moby dick | Tantrum + early BS 360 | Early, while inverted | Heelside | 4.5 | Tantrum to blind |
| Double half cab (mobe) | Two back rolls + FS 180 (360) | | Switch (heelside) | 5 | Back mobe |
| Rewinds (313 to blind and others) | Spin one way, then reverse | | Blind or wrapped | 5 | The base trick |
| Blind take-offs (blind KGB and others) | Take off riding blind | | | 5 | Riding blind |

### 3.5 Strapless

The GKA strapless list (2025 appendix B1):

- **Airs and spins:** air, air to blind, body 360, air reverse, toeside air 3, 5 and 7.
- **Rolls:** front and back rolls up to triples.
- **Passes:** FS and BS passes, 313, rodeo pass.
- **Board-offs:** one foot, board-off, hand stand, Jesus walk, hand varial, superman, tic tac,
  flip.
- **Kick tricks**, where the feet spin the board with no hand on it: shove-it 1, 2 and 3,
  varial flip, big spin. The board's spin must be timed to the airtime; it can land fins up.
- **Kite loops** with any of these.

Strapless needs a board that is not fixed to the feet. The board-off work in this plan builds
that.

### 3.6 Judging that the game's scoring should copy

- **Big air** (`research.md`): height, extremity, technicality and execution. The heat score is
  the best three tricks plus an impression score. Crashes score nothing. A repeated trick counts
  once.
- **GKA freestyle** (2025 rulebook, sections 21 and 28 to 33):
  - **Scores:** each trick scores 0.1 to 10.0, but only the best trick in each family counts.
  - **Count:** four tricks count, out of about seven attempts.
  - **Families:** the heelside group has four (raley-based; KGB and slim; hinterberger and
    heart attack; mobes). The variety group has five (rewinds; toeside and blind; combos;
    inverted doubles; kite-loop passes).
  - **Mix:** men count at most two heelside tricks and three variety tricks.
  - **Variety bonus:** by the number of families used, 1, 2, 4 or 7 points, for a 47-point
    maximum.
  - **Crashes:** a trick is a crash if the rider loses the bar or board, finishes the pass by
    pulling the leash, stops completely, or lands on their back and loses the board. A butt-check
    gets partial credit.
- **A clean freestyle landing:**
  - the pass finishes before the board touches down;
  - the board lands tail-first, pointing downwind;
  - the knees absorb the landing;
  - the rider rides away with speed;
  - the kite is still flying, slightly forward, at about 11 or 1 o'clock.

  WOO grades landings as crash, sketchy, good or stomped, from landing g and recovery.

The game's freestyle heat (T3.6, section 6.8) copies the GKA rules above: seven attempts, the best
trick per family, four counting with at most two heelside and three variety, the variety bonus, a
crash scoring nothing, and GKA's per-trick countdown (90 s).

## 4. What other games do, and what carries over

Full survey in the session notes; the patterns that matter here:

- **Rotation is committed at take-off.** SSX Tricky, Steep, Shredders, Infinite Air and Cool
  Boarders all pre-wind: hold a direction while loading, and the release sets the spin. Steep
  then lets stick pressure set the rotation speed and lands the rider when the stick is
  released early enough. This matches the sport (the rotation comes from the edge and the pop)
  and matches our input: the jump button already loads the edge.
- **Grabs are hand buttons plus a zone.** In Riders Republic, LT, RT or both pick the hand and
  the left stick picks one of four zones, which gives 12 grabs. Skate and Shredders put the
  hands on the triggers. Shredders speeds up the rotation while a grab is held, which is real
  physics: the tuck lowers the moment of inertia.
- **Physics first, names afterwards.** Shredders, Skater XL and Kiteboarding Pro play few or no
  canned trick animations. Rotation comes from the simulation, and a categoriser names what
  happened (Skater XL leaves naming to a mod). This fits a small C++ team with a procedural
  rig.
- **Re-catching is a deliberate input.** Session makes darkslides and primos need an input to
  catch them. Trials gives the pose a stick direction and makes the rider break out of it before
  landing. Both are good models for board-offs.
- **Landing grades.** THPS2 has perfect (+50%) and sloppy (−30%). Riders Republic has perfect,
  medium and bad, with a multiplier for consecutive perfects. OlliOlli forgives an early input
  more than a late one. No game publishes its angle thresholds.
- **Repeats.** THPS devalues a repeated trick to 100, 75, 50, 25 then 10%. Riders Republic pays
  its freestyle bonus once per move per run.
- **Recognition.**
  - Integrate angular velocity on fixed take-off axes.
  - Count inversions with hysteresis on the rider's up vector.
  - Snap spins to 180°.
  - Use a swing-twist decomposition at touchdown for the final tilt and heading.
  - Riders Republic sets landable orientations per family: spins every 180°, misty and rodeo
    every 1.5 rotations.
  - Skate samples the stick at 120 Hz because 60 Hz missed flicks; our 240 Hz simulation step
    covers that.
- **Camera.** Never roll with the rider; keep world up. Lag the follow so the spin reads on
  screen.

## 5. Where the code is today

These facts were read from `main` and from the physics worktree. Paths are under
`Source/KiteSurf/`.

**Body and rotation**

- The board and rider are one 85 kg point mass at the actor root (`BoardMesh`). The line force
  is applied there with no torque. Nothing in the code has angular velocity, inertia or a rider
  orientation.
- In the air, orientation is kinematic, in `UBoardMovementComponent::TickComponent`:
  - the carve stick yaws the board at `AirSpinRate` (200°/s);
  - with the stick released, the yaw turns back to the direction of travel, which undoes any
    spin;
  - pitch follows weight shift (±30°) and roll eases to 0.
- Nobody can invert.

**Landing**

- One test in the board component: the angle between velocity and the board axis, either end,
  must be at most `MaxLandingAngle` (30°). Pitch, roll, sink rate and kite position are
  ignored.
- `LandingG` is `|Vz| / 980`, which is mislabelled (physics `review.md` 4.2).

**Kite loops**

- `UKiteComponent` loops while the bar is held to the kite's own side (`bLooping`).
  `GetTurnDeg()` accumulates the turn, signed.
- The count is not tied to a jump: it builds up across loops and resets 0.75 s after a loop
  ends. `LoopSide` has no getter. Nothing records per-loop elevation, tension or timing. The
  HUD shows "KITE LOOP xN".

**Rig**

- `RiderRig::SolveBody` takes a *level* facing. The feet are fixed to the board transform, and
  the hands go to the bar.
- `SolveArms` takes arbitrary hand targets, so grabs are easy. Inversion and board-offs need
  rig changes.

**Input and events**

- Free inputs: X, Y, LB, RB, both stick clicks and the D-pad on the gamepad (the D-pad is used
  only in menus); Q, E, F, Shift, C and Z on the keyboard.
- Delegates exist for landing, crash, reset, kite crash and kite relaunch. There is none for
  take-off, apex or loops.

**Camera**

- The boom hangs off the root at (0, 0, 120). Pitching the root would swing the pivot.
- In the air the target yaw is clamped to the board yaw, so a spin sways the camera by 60°.

**Physics rework (`physics/rework`)**

- Moves the simulation into a 240 Hz fixed step owned by the pawn, with render interpolation.
- Treats any root transform change made outside the step as a teleport.
- Renames tuning values (`BaseJumpImpulse` becomes `PopImpulseKgCmPerS`, and others).
- Changes the test fixtures to one `Pawn->Tick(dt)` per frame.
- Will lengthen airtime (item 9: `8h/t²` is about 7 m/s² today against a real 1.5 to 3.5).
- Has no rigid-body rider and keeps the air orientation kinematic, so rider rotation is
  new trick work that does not collide with planned physics items. It forks from before #35 and
  still has to be merged with `main`.

## 6. Design decisions

### 6.1 A rotating rider on a point-mass trajectory

The trajectory stays a point mass, as `research.md` recommends. A new rotational state is added
for the rider in the air, stepped inside the pawn's fixed step:

- **State:**
  - body orientation `q` (world);
  - angular momentum `L` (world);
  - a diagonal inertia in the body frame that depends on the pose. Starting values, all
    **estimate**: about 1.5 kg·m² about Up with arms in; about 12 kg·m² about Side and Front
    when stretched out; about half that when tucked or grabbing. With the board on, add the
    board's inertia.
  - `ω = I_world⁻¹ L`, so a tuck speeds the rotation up and an extension slows it, with no
    extra rule.
- **Angular impulse at the pop:** from the pre-wind (section 6.3), scaled by load and by how
  hard the edge was. A pop with no pre-wind gives none.
- **Air control torque:** the left stick sets a target rate on the chosen axis. Torque is
  capped at about 30% of a full pre-wind per second (**estimate**), so the take-off still
  decides most of the rotation.
- **Line torque:** the line force acts at the hook while hooked in, and at the hands while
  unhooked. The torque is `r × F` about the centre of mass:
  - **Hooked in:** the hook is 0.10 to 0.15 m in front of the centre of mass (**estimate**,
    `research.md`). The torque pulls the body's Up towards the lines and leaves rotation about
    the lines nearly free. That is why big air rolls are thrown with the kite parked and turn
    roughly about the lines, and why a megaloop swings the body horizontal.
  - **Unhooked:** the lever is an arm's length, 0.5 to 0.7 m. A pop with the kite low and the
    lines loaded then swings the body out horizontal: the raley falls out of the model instead
    of being scripted.
  - **Scale:** the torque is multiplied by `LineTorqueScale` and calibrated so that a
    full-input back roll at hang tension takes 1.5 to 2.5 s.
- **Landing assist:**
  - **When it acts:** there is no rotation input, the predicted time to contact is under
    0.7 s, and the rider is within ±60° of a valid landing attitude.
  - **What it does:** a PD torque turns the rider towards the nearest valid attitude: upright,
    yaw to the nearest 180°.
  - **Strength:** set by the assist setting. Over- and under-rotation can still fail.
  - **Time to contact:** recomputed every step from height, sink rate and measured vertical
    acceleration, because the kite makes the fall far from ballistic.
- **Where it lives:** a new `URiderAttitudeComponent` on the pawn. It has a
  `Step(float Dt, const FAttitudeInputs&)` that the pawn calls after the board step in its
  fixed step, plus `GetBodyQuat()`, `GetAngularVelocity()` and `GetAttitudeState()`. It is pure
  enough to test without a world. The physics branch's `StepSimulation` is the call site. Until
  that merges, the pawn's `Tick` calls it after the board component.
- **What it replaces:** the kinematic `AirSpinRate` yaw and the auto-align in
  `UBoardMovementComponent`. While strapped in, the board's air orientation becomes
  `q × StrapOffset`.

### 6.2 Physics root and visual board

`BoardMesh` is the root and the physics body, and the camera boom hangs off it. Rotating it
through rolls would swing the camera pivot and fight the physics rework's interpolation.

- **Split it.** The root keeps a yaw-only rotation in the air: the board heading used for
  hydrodynamics and landing. A new `BoardVisual` static mesh child takes the full rotation, from
  the attitude component while strapped in and from the hand socket during a board-off.
- **Ripple effects.** The gear preview, the wake component and the tests that read
  `BoardMesh` need updating in the same change.
- **Camera.** In the air, drive it from velocity and the kite, not from the board yaw.

### 6.3 Input mapping

This builds on what is bound today (`scripts/editor/make_input_assets.py`). Every new action
gets a keyboard binding, a legend entry (`KiteSurfControlsLegend.cpp`) and a line in the input
assets test.

| Action | Gamepad | Keyboard | Notes |
| --- | --- | --- | --- |
| Load and pop (exists) | A, hold and release | Space | |
| Rotation modifier (batch A, `docs/tricks/review.md` section 4) | LT, digital past half travel | Left Shift | Held, the left stick reaches the pre-wind while loading and the rotation in the air; without it the stick is always the board's carve and weight shift, so a plain jump stays straight |
| Pre-wind | Left stick held while loading, with the modifier | A/D and W/S held while loading, with Shift | Direction picks the rotation, the hold sets its size. It builds over about 0.5 s up to a cap |
| Rotation in the air | Left stick, with the modifier | A/D, W/S, with Shift | X alone: a roll, back or front, at 65° of inversion (`DefaultRollAxisTiltDeg`, unified with the no-pre-wind default by batch A). X plus Y: Y tilts the axis, down towards more inverted, up towards a spin (`SpinAxisTiltMaxDeg`, 25°). Y alone: flip (pull for a backflip, push for a front flip). Full flips are unhooked: hooked in, a Y pre-wind is scaled by `HookedFlipScale` (0.35, T3.3), and unhooked a pulled Y is the tantrum (the back hand comes off at take-off). Rider-relative, so the stick towards the tail on screen is a back roll |
| Grab, front hand | LB | Q | While held, the left stick picks the zone: nose, toe edge, heel edge or tail, whatever the modifier is doing. The rotation keeps its momentum |
| Grab, back hand | RB | E | As above |
| Board-off | LB + RB held | Q + E | Left stick picks the variant: plain, superman, tic tac. Letting go starts the re-catch, which takes about 0.3 s (**estimate**). Not caught by touchdown is a crash |
| One-footer | Left stick click | C | Back foot out while held |
| Unhook or hook in | Y | F | On the water only. Freestyle |
| Handle pass | X | X (moved off Left Shift in batch A, which is the rotation modifier now) | In the air, unhooked. FS or BS comes from the current spin sense |
| Kite (exists) | Right stick, RT | Arrows, right mouse | Loops stay on the bar; the gesture loop is backlog D3. LT moved to the rotation modifier in batch A and no longer sheets |
| Recentre the motion bar (exists) | Right stick click | Home | Only while the motion bar follows a controller, when the right stick is idle |

The motion bar frees the right stick anyway, so none of the above conflicts with it.
`kitesurf.Input` gains arguments for load, pre-wind, grab, board-off and pass, so scripted rides
and the menu-video recorder can perform tricks.

### 6.4 Unhooked riding (freestyle)

- **State:** `bHooked` on the pawn, toggled on the water. While unhooked:
  - **Line force:** acts at the hands (section 6.1).
  - **Grip limit:** if the line tension passes `UnhookedGripLimitBW` × body weight (start at
    1.3, **estimate**) for more than 0.15 s, the bar is pulled from the hands. The kite
    depowers on the leash and the jump scores as a crash. This is the skill: a kite kept low and
    not sheeted in too far.
  - **Kite assist:** the kite parks at about 45° elevation instead of drifting to 12, so a pop
    gives a freestyle jump instead of a sent one.
- **Handle pass** (bar states):
  - The bar is held by both hands, the front hand only, the back hand only, wrapped, or behind
    the back (blind).
  - A pass needs line tension under `PassSlackTensionBW` (0.3, **estimate**) for
    `PassDurationSeconds` (0.25 s, **estimate**), with the body's back turned to the kite
    (±60°).
  - If tension rises during the pass, the bar is lost.
  - The "flick" assist dips the kite for slack when the pass button is pressed.
- **Stance on the water** (T3.5, done; docs/movement.md "Landing stances"):
  - The pawn has a riding stance (`ETrickStance`, `GetRidingStance`, `GetStanceSeconds`). An
    unhooked touchdown sets it from the body's heading against the kite and the bar's route
    (`StanceForWrap`): a backside 180 lands blind (the bar behind the back), a frontside 180
    toeside (the bar in front, the torso twisted `ToesideTorsoTwistDeg`, 70, back towards the
    kite). Hooked in it is always heelside, and the rider slides round as before.
  - Toeside is held `ToesideHoldSeconds` (3.0 s) and blind `BlindHoldSeconds` (2.0 s), both
    **estimates**, before the slide round brings the rider back to heelside.
  - X on the water ends it early: from blind a surface pass (tension under
    `SurfacePassMaxTensionBW`, 0.6, **estimate**; `SurfacePassSeconds`, 0.4 s) and the backside
    half turn on to heelside; from toeside the half turn back the way the frontside 180 came (it
    unwinds the lines round the front; a further frontside half turn would wrap them). A surface
    pass started within the surface grace of the touchdown joins the jump's record.
  - Blind and toeside take-offs, which open the GKA variety families, need riding in those
    stances (T3.7). That comes last.

### 6.5 Poses and the rig

`FRiderRigInput` gains:

- the full body quaternion, replacing the level `Facing`;
- a per-foot attached flag and ankle target (one-footer, board-off);
- per-hand targets: bar, a board socket, a free pose, or behind the back.

The leg pole uses the body frame rather than world-level facing.

**Pose library.** A small table of joint-target offsets in rider space, each with an inertia
scale:

- neutral, crouch and tuck;
- grab per zone;
- board-off extended and superman;
- raley extension and handle-pass reach.

Poses blend with a critically damped spring, 0.12 to 0.2 s (**estimate**). Grabs are IK to
sockets on the board, using the existing `SolveTwoBone`. There are no authored animations.

The robot rider uses the jointed rig too (#54), so every rider can do tricks.

### 6.6 Recognition

A new `UTrickTracker` (an actor component on the pawn). Its logic is in pure functions so tests
can feed it synthetic data.

**At take-off it freezes a frame:**

- U = world up;
- T = horizontal travel direction;
- S = U × T.

It also records the take-off stance and whether the rider is hooked in.

**Each simulation step it:**

- adds the angular velocity onto U, T and S, and onto the body's Up, Front and Side;
- counts inversions with hysteresis on `dot(BodyUp, U)`: armed above +0.3, counted below −0.3;
- classifies each inversion as a roll or a flip from the dominant axis in the body frame, and
  as back or front from its sign;
- adds up the kite's heading turn from `GetKiteHeading()` itself, so it does not depend on
  `bLooping` ending when the bar is centred, and keeps a loop record (below);
- logs the time spans of grabs, one-footers, board-offs and passes.

Each kite loop record holds:

- direction;
- start time relative to take-off and to the apex;
- rider height at the start;
- start and minimum kite elevation;
- peak tension;
- duration;
- whether it completed.

**Loop classification** (all thresholds **estimate**, to be tuned after physics items 7 to 9):

| Loop | Rule |
| --- | --- |
| Heli loop | Started after the apex, kite at 40° or more throughout, dropping no more than 25° (a loop starts only 35° round the clock and bottoms about 20° below where it starts, so an absolute 55° limit could never be met) |
| Kite loop | 360° completed, minimum elevation under 55° |
| Megaloop | A kite loop started with the rider 8 m or more up, minimum elevation 20° or less, peak tension 3 body weights or more (`research.md` C6) |
| Contra loop | Looped against the natural direction, where natural means the kite dives in the rider's direction of travel (pulled with the front hand). Direction to be checked against footage |
| Double or triple | Two or three completed loops in one airtime |
| S-loop | 180° or more one way, then 180° or more the other way; a snake loop adds one more |
| Early or late roll | The roll starts before or after the loop's tension peak (the yank) |

**At touchdown:**

- `qRel = qLand × qTakeoff⁻¹` is split by swing-twist about U into a tilt (for the landing
  grade) and a net heading (heelside, switch, blind or toeside).
- Whole turns come from the integrals, not from `qRel`.
- Spins are credited in half turns: `floor((|spin| + 45°) / 180°)`.
- Guard the swing-twist near 180°, where it is ill-conditioned.

**Output: an `FTrickSignature`:**

- take-off stance, hooked in or not;
- inversions (type, sense, count);
- spin half turns and their sense;
- raley or S-bend;
- loops;
- grabs (name and hold time);
- one-footer, board-off (variant and time);
- passes (count, sense, degrees);
- landing stance and landing grade.

**Naming.** `TrickNaming::Name(const FTrickSignature&)` is a pure function:

- **Big air** names compose: `[Double] [Late] {Megaloop | Kiteloop | Contra loop | S-loop}
  [Back roll | Front roll] [Board-off | Tic tac] [Indy] [to blind]`.
- **Freestyle** names come from a table keyed by take-off stance, take-off move, pass sense and
  degrees, and landing state: KGB 5, Slim 7, 313, Back to blind.
- **Fallback:** a combination with no table entry gets a plain description such as "Back roll
  + backside 540 pass to blind". Shredders caps its categoriser at a maximum count; do the
  same.

**The family key** (for repeats and GKA family limits) is the signature without grab hold
times and with spins rounded.

### 6.7 Landing evaluation

`UBoardMovementComponent`'s single angle test becomes a call to a landing evaluator, so physics
item 12 (landing g from absorb distance) can change its inputs without touching trick code.

`FLandingInputs`:

- tilt of the board from the water normal;
- board yaw against velocity (either end, so switch counts);
- sink rate and landing g;
- kite elevation;
- board attached;
- bar in the hands, or a pass still in progress;
- rider inverted.

It returns an `FLandingVerdict` with a grade and a cause. The grades are stomped, clean,
sketchy and crash, using WOO's words. Thresholds (**estimate**):

| Grade | Tilt | Yaw off velocity | Other |
| --- | --- | --- | --- |
| Stomped | ≤ 15° | ≤ 20° | Kite at or above 45° elevation, landing g ≤ 4 |
| Clean | ≤ 30° | ≤ 45° | |
| Sketchy | ≤ 50° | ≤ 75° | Or a hot landing (kite under 45°); rides away slower |
| Crash | Beyond | Beyond | Or board off, bar lost, pass unfinished, inverted at contact |

The cause feeds the one-line failure message (backlog F6): "board not caught", "pass not
finished", "over-rotated", "kite too low".

`OnBoardLanding` gains the verdict, and a new `OnBoardTakeoff(bool bPopped)` fires from
`BeginAirborne()`.

### 6.8 Scoring

Use the per-jump formula from `research.md`:
`Height × (1 + Extremity) × (1 + Technicality) × Execution`. Starting values (all **estimate**):

- **Height:** `h^1.15` in metres.
- **Extremity:** 0.5 per kite loop × lowness (`1 − minElevation / 60°`) × lateness (rider
  height at loop start ÷ apex). An extra 0.3 for a contra loop or an S-loop.
- **Technicality:** sum the elements:

  | Element | Points |
  | --- | --- |
  | Each inversion | 0.4 |
  | Spin, per 180° | 0.15 |
  | Grab (held at least 0.3 s) | 0.2, plus up to 0.2 more for hold time |
  | One-footer | 0.2 |
  | Board-off | 0.6 |
  | Each handle pass | 0.4 |
  | Unhooked | 0.3 |
  | Blind or toeside landing | 0.2 |

- **Execution:** stomped 1.0, clean 0.85, sketchy 0.5, crash 0. Real judging scores crashes as
  nothing. Backlog F4's "30% or less" is met by 0; the open questions ask whether free ride
  should show a partial score.
- **Repeats in free ride and sessions:** keyed on the family key, 100, 75, 50, 25 then 10%.
- **Heats:**
  - big air: best three plus an impression or variety score;
  - freestyle: best per GKA family, four counting with the group limits, and a variety bonus of
    1, 2, 4 or 7.

Scoring is pure functions over `FJumpRecord` and `FTrickSignature`, in `TrickScoring.h/.cpp`.
That keeps it testable now and easy to retune.

**Best-three session (T2.5, backlog F5).** A timed session, 90 s by default, started with
`kitesurf.Session [seconds]` or the pause menu's "Best-three session (90 s)". The pure rules are
`FBestThreeSession` (`Tricks/SessionScoring.h`); `UTrickSessionSubsystem` feeds it from the
rider's trick tracker by polling the record count.

- **The window:** a jump counts if it took off at or after the start and before the horn. A jump
  in the air at the horn that took off before it counts at its landing, as in real heats: the
  session goes to overtime and ends at that landing (at most 15 s later). A take-off after the
  horn never counts, and neither does a jump already in the air at the start.
- **Repeats:** the session counts landings per family key from zero (free-ride repeats do not
  carry over) and pays 100, 75, 50, 25, then 10%.
- **The repeat rule (F5 against GKA):** each family key counts once, with its best paid landing,
  and the total is the best three of those. So a repeat of the same quality never raises the
  total, as F5 asks; a repeat raises it only by beating the counted landing by more than 1/0.75
  on its raw score, which is GKA's "counts once, the best one". Three identical straight airs
  total one straight air.
- **Crashes** score 0 and never count, and do not use up a family's full-value landing. Jumps
  under 1 m are ignored, so a hop does not use it up either.
- **Clock:** the board's simulation time, so the pause menu pauses the session.
- **HUD:** the clock and the three counting scores at the top centre while it runs; a results
  card at the end with the total, the three counting jumps (name, height, points), "NEW BEST" and
  the local best.
- **Local best:** per session length, in the save game (`BestSessionTotalBySeconds`), saved
  through the settings save path when beaten.

The tests are `KiteSurf.Trick.Session*` in `TrickSessionTests.cpp`.

**Freestyle heat (T3.6).** A GKA-style heat of seven attempts by default, started with
`kitesurf.Heat freestyle [attempts] [countdown s]` or the pause menu's "FREESTYLE HEAT (7 tricks)"
(free ride only, after SCHOOL, so the items before it keep their places). The pure rules are
`FFreestyleHeat` (`Tricks/FreestyleHeat.h`) around `ScoreFreestyleHeat`; `UFreestyleHeatSubsystem`
feeds it from the rider's trick tracker by polling the record count, as the session does.

- **An attempt** is an unhooked jump with more than 0.4 s of airtime, or any crash (bar lost, board
  lost, a crash landing; hooked or not), which scores 0. A landed hooked jump is not an attempt and
  the notice line says "Unhook for freestyle"; neither is a landed unhooked hop of 0.4 s or less, nor
  a jump that took off before the start. An unhooked jump with no freestyle family (a plain unhooked
  pop) uses its attempt and never counts, as a wasted trick does in a real heat.
- **Scoring** each attempt from its record: the signature rebuilt with `SignatureFromJump`, the grade
  the record's (the board's verdict), then `FreestyleTrickScore` with the record's apex in metres
  (family and difficulty from `TrickNaming::FreestyleFamily`), and the heat with `ScoreFreestyleHeat`.
- **The trick countdown** (GKA format): 90 s per attempt, restarted by each attempt; when it runs out
  on the water the attempt is lost and scores 0 ("Trick 3 lost: time ran out"). A jump in the air
  that took off in the heat holds it until its record decides. `kitesurf.Heat freestyle 7 0` turns
  it off.
- **Clock:** the board's simulation time, so the pause menu pauses the countdown.
- **One mode at a time:** a heat does not start while a best-three session or a lesson runs (a notice
  says why), and a running heat is cancelled when either starts.
- **HUD:** a row at the top centre ("FREESTYLE HEAT", "Trick 3/7", the countdown, "TOTAL 27.5 (+4
  variety)") and the counting list at the right (up to four, with their families, "--" while empty);
  at the end a results card with the total, the counting tricks, the families used, the bonus,
  "NEW BEST" and the local best.
- **Local best:** per attempt count, in the save game (`BestFreestyleHeatTotalByAttempts`), saved
  through the settings save path when beaten.

The tests are `KiteSurf.Trick.FreestyleHeat*` in `TrickHeatTests.cpp` (the pure heat score,
`FreestyleHeatScore`, is in `TrickFreestyleTests.cpp`).

### 6.9 HUD and feedback

**Trick ticker:** a Canvas element in `AKiteSurfHUD`, modelled on `ShowNotice`.

- Parts appear as they are credited: "Back roll", then "Double back roll", then "+ Indy 1.2 s".
- On landing it finalises into a jump card: name, height, airtime, landing g, grade (coloured),
  score and the failure cause.
- This also covers backlog E5's jump card.

**Feedback:**

- the existing pop and landing one-shots, plus a rotation whoosh whose pitch follows the spin
  rate, and a grab snap;
- rumble on a stomped landing;
- the camera dollies out during rotations and never rolls.

## 7. Implementation plan

Sizes: S is under a day, M a few days, L a week or more. Every task ships with
`KiteSurf.Trick.*` automation tests and follows the merge-to-main rule. Test names below are the
acceptance criteria.

### Milestone T0: groundwork (no dependency on the physics rework)

| ID | Task | Acceptance (tests) | Size |
| --- | --- | --- | --- |
| T0.1 | Shared test fixture. Move `FRideFixture` and `RunJump` from `RideLoopTests.cpp` into `Tests/KiteSurfTestFixtures.h` behind one `Simulate(Seconds)`, so the physics rework's fixture change is one edit | Existing tests unchanged and green | S |
| T0.2 | Jump events and record. Add `OnBoardTakeoff` and `OnBoardApex`; an `FJumpRecord` per jump with take-off and landing time, apex, airtime, landing g fixed to a real value, peak tension, distance and kite loop records; kept for the session. Overlaps backlog C8 | `KiteSurf.Trick.JumpRecordMatchesTrajectory`: apex within 0.1 m of the sampled maximum, one record per jump | M |
| T0.3 | Kite loop records. Per-loop data from the kite heading turn, independent of `bLooping`; expose the loop direction | `KiteSurf.Trick.LoopRecordCountsAndDirection`: two held loops give two records with the right sign and a minimum elevation below the start | S |
| T0.4 | Signature, naming and scoring as pure functions, with table-driven big air and freestyle names | `KiteSurf.Trick.NamesBigAir` (megaloop back roll, double kiteloop, contra loop board-off); `KiteSurf.Trick.NamesFreestyle` (KGB 5, 313, back to blind, slim 7); `KiteSurf.Trick.ScoreOrdering` (20 m loop beats 20 m straight; a low megaloop beats a high-kite loop at the same height; a crash scores 0; a repeat scores less) | M |
| T0.5 | Jump card and ticker on the HUD, showing height, airtime and grade from the existing landing result | `KiteSurf.HUD.JumpCardShowsAfterLanding` | S |

### Milestone T1: first rotations (needs physics items 1 and 2 merged to `main`)

| ID | Task | Acceptance (tests) | Size |
| --- | --- | --- | --- |
| T1.1 | Visual board split (section 6.2), camera air mode | `KiteSurf.Pawn.CameraStaysLevelThroughRoll`; the gear preview and wake tests pass | M |
| T1.2 | `URiderAttitudeComponent`: inertia by pose, pre-wind impulse, capped air torque, line torque at the hook, stepped in the fixed step; replaces the kinematic air yaw | `KiteSurf.Trick.BackRollFromPreWind` (one inversion in 1.5 to 2.5 s at 30 kn); `KiteSurf.Trick.TuckSpinsFaster`; `KiteSurf.Trick.NoInputNoRotation`; `KiteSurf.Trick.StepRateIndependentRotation` (30, 60 and 120 fps within 2°) | L |
| T1.3 | Rig takes the body quaternion; inverted poses | `KiteSurf.Rider.InvertedKeepsFeetInStraps` | M |
| T1.4 | Pre-wind and air rotation input, gamepad and keyboard, plus `kitesurf.Input` arguments | `KiteSurf.Input.AssetsValid` covers the new actions | S |
| T1.5 | Landing evaluator and assist | `KiteSurf.Trick.LandingGrades` (tilt and yaw tables); `KiteSurf.Trick.SwitchLandingIsClean`; `KiteSurf.Trick.UnderRotatedRollCrashes`; `KiteSurf.Trick.AssistLandsNearMiss` | M |
| T1.6 | Tracker live: inversions, spins, loops, landing stance into the signature and ticker | `KiteSurf.Trick.RecognisesMegaloopBackRoll` on a scripted ride | M |

Exit test: in a `-game` run, a player can throw a back roll and a megaloop back roll, see them
named and scored, and crash an under-rotation.

### Milestone T2: big air depth

| ID | Task | Acceptance (tests) | Size |
| --- | --- | --- | --- |
| T2.1 | Grabs: hand plus zone, IK to board sockets, hold timing, tuck inertia | `KiteSurf.Trick.GrabNamesByHandAndZone`; `KiteSurf.Trick.GrabHoldCounts` (0.3 s minimum) | M |
| T2.2 | One-footer | `KiteSurf.Trick.OneFooterFootReturns` | S |
| T2.3 | Board-off: board follows the hand socket, re-catch, variants (plain, superman, tic tac, board pass) | `KiteSurf.Trick.BoardOffNotCaughtCrashes`; `KiteSurf.Trick.BoardOffCaughtLands` | M |
| T2.4 | Loop families: contra, double and triple, S-loop and snake, heli-loop landing, early or late roll; the kite's in-air loop entry so that S-loops and contra loops can be flown | `KiteSurf.Trick.ClassifiesLoops` on recorded loop sets; `KiteSurf.Kite.AirLoopFromAnyClock`, `KiteSurf.Kite.AirReverseMakesSLoop`, `KiteSurf.Kite.WaterLoopEntryUnchanged`, `KiteSurf.Kite.AirContraLoopIsContra` | S |
| T2.5 | Best-three session and repeat devaluation (backlog F5) | `KiteSurf.Trick.SessionBestThree`, `SessionCrashScoresZero`, `SessionTakeoffBeforeHorn`, `SessionOnPawn`, `SessionHornMidAirOnPawn`, `SessionHUDFormatting`, `SessionBestPersists`, `SessionOldSaveLoads`, `SessionStartsFromPauseMenu` | S |
| T2.6 | Failure cause messages (backlog F6) | `KiteSurf.Trick.FailureCauseNamed` | S |
| T2.7 | Trick book: tricks landed, saved with the save game (backlog G4) | `KiteSurf.Trick.TrickBookPersists` | S |

### Milestone T3: freestyle

| ID | Task | Acceptance (tests) | Size |
| --- | --- | --- | --- |
| T3.1 | Unhook and hook in; line force at the hands; grip limit; low-park kite assist | `KiteSurf.Trick.UnhookedOverpowerLosesBar`; `KiteSurf.Trick.UnhookedPopIsBallistic` (1 to 4 m, `8h/t²` within 30% of g) | M |
| T3.2 | Raley from line torque, S-bend from an added spin | `KiteSurf.Trick.RaleyFromUnhookedPop` (body over 60° from upright with no inversion) | M |
| T3.3 | Flips (tantrum, front flip) on the Y axis while unhooked | `KiteSurf.Trick.TantrumIsBackFlip` | S |
| T3.4 | Handle pass: bar states, slack window, flick assist, air or surface pass | `KiteSurf.Trick.PassNeedsSlack`; `KiteSurf.Trick.NamesKgbFromRide` | M |
| T3.5 | Landing stances: toeside and blind held after landing, then ride away or slide round | `KiteSurf.Trick.LandsBlindAfterBackToBlind` | M |
| T3.6 | Freestyle heat: GKA families, four counting, group limits, variety bonus; the heat flow (attempts, trick countdown, HUD, local best) | `KiteSurf.Trick.FreestyleHeatScore`; `FreestyleHeatAttempts`, `FreestyleHeatCountdown`, `FreestyleHeatOnPawn`, `FreestyleHeatHUD`, `FreestyleHeatBestPersists` | S |
| T3.7 | Riding toeside and blind; blind and toeside take-offs | Movement tests for each stance | L |

### Milestone T4: strapless (later, optional)

Strapless board, shove-it (the feet spin the board), board-off variants without straps, and
first-trick-per-tack scoring. This builds on T2.3's detached board.

### Order against other work

- **The fixed step is on `main`** (#45), so nothing here waits for it any more.
- **Physics phase 2** (`docs/physics/plan-2.md`) edits `BoardMovementComponent`, `KiteComponent`
  and the physics tests. The trick work that changes those files waits for phase 2 to merge:
  - stepping the rider attitude and replacing the kinematic air spin (T1.2 PR E);
  - air rotation input (T1.4) and live rotation tracking (T1.6);
  - the kite's loop entry for S-loops and contra loops (done: in the air a full bar held for 0.3 s
    loops the kite from any clock and a full bar reversed mid-loop and held starts a loop the other
    way, so a keyboard tap still flies the kite across; `AirLoopHoldSeconds`,
    `UKiteComponent::AirLoopFullBarThreshold`, `docs/jumping.md`; until then S-loops could not be
    flown and a contra loop needed the kite parked 35 deg round on the side the rider came from);
  - the board's own take-off and landing values.

  The status table in `docs/tricks/README.md` lists what has landed.
- **Tuning:** wait for physics items 7 to 9 (kite terms, rider drag, hang time) before tuning
  rotation rates, loop thresholds or scores. Airtime will change.
- **Landing:** physics items 10 and 12 (force-balance edging, multi-point water and landing
  from absorb distance) change the landing evaluator's inputs only.
- **Backlog overlap:** T0.2 overlaps backlog C8, T1.5 C7, T2.4 C6, T0.4 F3 and F4, T2.5 F5, T2.6
  F6, and T0.5 E5. This plan supersedes F1 to F6 in `research.md`.

## 8. Tuning starting points

Every value is an **estimate** unless tagged otherwise.

| Value | Start | Why |
| --- | --- | --- |
| Big air back roll duration | 1.5 to 2.5 s | Rolls inside 5 to 10 s airtimes |
| Freestyle 360 | 0.5 to 0.9 s | Inside 1.3 s airtime (**sourced** airtime) |
| Inertia: spin / roll and flip stretched / tucked | 1.5 / 12 / 6 kg·m² | Human body plus board, rough |
| Air torque cap | 30% of a full pre-wind per second | Rotation decided mostly at take-off |
| Hook lever / hands lever | 0.12 m / 0.6 m | `research.md`; arm length |
| Landing assist window | 0.7 s before contact, ±60° | Steep and Riders Republic behaviour |
| Landing grades | Section 6.7 table | No game publishes its thresholds |
| Grab minimum hold | 0.3 s | |
| Board-off re-catch | 0.3 s, with 0.12 s of grace | OlliOlli-style early forgiveness |
| Unhooked grip limit | 1.3 body weights for 0.15 s | |
| Pass slack and duration | Tension under 0.3 body weights for 0.25 s | Pass window 0.2 to 0.4 s (**typical** timing) |
| Megaloop | Rider at 8 m or more, kite at 20° or less, tension 3 body weights or more | `research.md` C6 |
| Air loop full bar | 0.85 of the bar, at the kite after the dead time | In the air a bar this far over loops the kite from any clock (`AirLoopFullBarThreshold`); less still flies it across, for the redirect |
| Air loop hold | 0.3 s of full bar, at the kite after the dead time | Before the full bar starts or reverses a loop in the air (`AirLoopHoldSeconds`); a shorter tap flies the kite across, so arrow keys (always a full bar) can still steer it |
| Freestyle unhooked pop | 5.4 to 7.4 m/s vertical | **derived** from 1.1 to 1.5 s airtime |

## 9. Open questions

- [ ] **Rotation feel.** The plan commits rotation at take-off (pre-wind) and caps air torque.
  The alternative is arcade-style free rotation in the air. Recommended: the physical version,
  with the landing assist on by default.
- [ ] **Freestyle scope.** It is milestone T3, after big air depth. Riding toeside and blind
  (T3.7) is large; without it, freestyle still gets blind and toeside landings, but not the
  variety families.
- [ ] **Sign conventions.** Back/front and FS/BS are defined in section 2 from coaching text.
  Confirm them against one reference clip each (back roll, KGB, back mobe, contra loop) before
  T1.2 and T3.4 fix them in tests. Two cases are already pending:
  - `RollInversionSign` in `Tricks/RiderAxes.h`: −1 tilts the roll axis away from the lines,
    +1 towards them.
  - The contra rule: a plain held loop in the game reads as a contra loop. Since the in-air loop
    entry a loop either way can be flown from overhead, and riding right a full bar to the left is
    named contra (`KiteSurf.Kite.AirContraLoopIsContra`), on the sign in
    `TrickRecognition::IsContraLoop`, which is still to be checked against footage.
- [ ] **Freestyle names for odd numbers.** Under the wrap model, KGB 5 and 315 land blind or
  toeside. The name table expects heelside, so those jumps are described rather than named.
  Which landing does a real KGB 5 have?
- [ ] **Crash score in free ride.** Real judging gives 0. Should free ride show a partial score
  for learning?
- [x] **Robot rider.** It uses the jointed rig (#54).
- [x] **Visual board split.** Done in #68. The gear preview is a separate actor and was not
  affected.

## Sources

Big air basics, King of the Air and GKA big air sources are listed in `docs/research.md`.

**Rules and trick lists**

- [GKA Rulebook 2025](https://www.gkakiteworldtour.com/rulebooks/2025/GKA%20RULEBOOK%202025.pdf):
  sections 21, 24, 27 to 33, appendices A to C.
- [GKA Freestyle World Cup France 2023 notice](https://www.gkakiteworldtour.com/wp-content/uploads/2022/11/PDF-Race-Notice-GKA-Freestyle-Kite-World-Cup-France-1.pdf):
  variety bonus, 4 counting tricks of 7.
- [GKA freestyle discipline](https://www.gkakiteworldtour.com/discipline-freestyle/),
  [GKA big air trick list 2023](https://www.gkakiteworldtour.com/wp-content/uploads/2023/04/Tricklist-Big-Air-TT-2023.pdf).
- [Universkite trick dictionary](https://universkite.fr/dictionnaire-kitesurf-lexique-definition-glossaire-vocabulaire-kite/).
- Wakeboarder.com trick lists:
  [handle-pass inverts](http://www.wakeboarder.com/tricks/handlepass.phtml),
  [inverts](http://www.wakeboarder.com/tricks/inverts.phtml),
  [raley-based](http://www.wakeboarder.com/tricks/raleybased.phtml),
  [spins](http://www.wakeboarder.com/tricks/spins.phtml).

**Technique**

- IKSURFMAG: [back mobe](https://www.iksurfmag.com/technique/advanced/back-mobe/),
  [blind judge](https://www.iksurfmag.com/technique/advanced/blind-judge/),
  [NIS](https://www.iksurfmag.com/technique/advanced/nis/),
  [unhooked jump](https://www.iksurfmag.com/technique/intermediate/unhooked-jump/),
  [inverted back roll](https://www.iksurfmag.com/technique/intermediate/inverted-back-roll/).
- MACkite: [unhooked pop](https://www.mackiteboarding.com/news/how-to-pop-kiteboarding-unhooked-the-ultimate-guide-/),
  [raley](https://www.mackiteboarding.com/news/how-to-do-an-unhooked-raley/),
  [surface pass](https://www.mackiteboarding.com/news/the-surface-pass-kiteboarding-/),
  [back roll kiteloop late tail grab](https://www.mackiteboarding.com/news/how-to-backroll-kiteloop-late-tail-grab-with-shaun-bennett/).
- [Kiteboarding St Petersburg: KGB](https://kiteboardingstpetersburg.com/how-to-kgb/).
- [Elite Watersports grab list](https://elitewatersports.com/blogs/tips-and-tricks/kiteboarding-grab-list-pictures-and-infographic).
- [Reo Stevens: 360 shove-it](https://reostevens.com/kitesurfing-tips-tutorials/straplessfreestyle/kitesurfing-360-shove-1358/).

**Measurements**

- [Simons et al. 2025](https://pubmed.ncbi.nlm.nih.gov/41189695/): freestyle and big air height,
  airtime, landing g.
- [WOO freestyle scoring](https://docs.woosports.com/docs/kite-freestyle).

**Games**

- [Skate's text prototype and 120 Hz sampling](https://www.gamedeveloper.com/game-platforms/ea-s-blackwood-text-based-prototype-perfected-i-skate-i-s-control-scheme).
- [Riders Republic control presets](https://www.gamespot.com/articles/riders-republic-stunts-guide-what-to-know-about-the-various-control-options/1100-6497519/),
  [landing modes](https://www.ubisoft.com/en-us/game/riders-republic/news-updates/2HFvoOtBvWRtrlXOF8ZXjo/answering-your-riders-republic-questions).
- Shredders: [animation approach](https://premortem.games/2022/03/21/shredders-shows-that-indie-developers-can-do-realistic-sports-games/),
  [developer interview](https://kotaku.com/shredders-xbox-series-x-s-pc-snowboarding-game-pass-foa-1848733449).
- [Steep trick system](https://gamerant.com/steep-trick-system-tips/).
- [THPS2 landing grades](https://strategywiki.org/wiki/Tony_Hawk's_Pro_Skater_2/Gameplay).
- [Session and Skater XL](https://news.xbox.com/en-us/2022/09/22/learning-to-get-back-on-the-board-with-session-skate-sim/).
- [Kiteboarding Pro developer article](https://www.surfertoday.com/kiteboarding/kiteboarding-pro-the-kitesurfing-game-for-everyone).
- [Swing-twist decomposition](http://marc-b-reynolds.github.io/quaternions/2022/01/31/QuatAxisFactor.html).

Claims taken from search summaries rather than a full page read: the tantrum technique, early
against late back roll in a loop, the kung fu definition, and King of the Air 2019. Every
rotation rate and duration tagged **estimate** is derived from airtime, not measured.
