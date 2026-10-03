# Trick system review: as built, and the plan to make it play well

Reviewed on `tricks/rework` at `0c2f4fd` (`origin/main` after #108), 2026-10-03. Line numbers are
against that commit.

**How this was checked.** Code reading. `scripts/build.sh Development` and
`scripts/run-tests.sh -nullrhi` were run once at `0c2f4fd`: `Test Results: Total=366, Succeeded=366,
Failed=0`. Numbers marked *(log)* come from that run's `AddInfo` lines, so they "pass under
`-nullrhi`". Nothing here was seen in a `-game` run. The user's report ("the kiter just rolls on any
flip") is the only play evidence.

## Summary

- **The roll on every jump is the design working as written.** It is not a stray torque. The left
  stick is the board's carve (X) and weight shift (Y). With jump held on the water the same stick
  becomes the pre-wind, and in the air it becomes the rotation stick. The pawn re-routes a held stick
  every frame, so nothing needs to be pressed again. The game teaches players to hold that stick
  through the take-off: the README says weight on the tail (S), and the school says "edge hard
  upwind" and "Hold the edge until take-off". So a normal jump leaves the water with a full roll or
  backflip pre-wind.
- **Everything else is wired into play.** The jump record, recognition, grading, naming, scoring,
  HUD card and ticker, best-three session, freestyle heat and trick book all run during play. No
  `Tricks/` header is used only by tests. The gaps are in feel and integration:
  - rotations whose result depends on line tension rather than the player;
  - every big jump graded SKETCHY;
  - no audio, haptic or camera response to a trick;
  - a trick book that is not saved when a trick is first landed.
- **Batch A** puts rotation behind a held modifier, `IA_Rotate`: LeftShift on the keyboard and the
  left trigger on the gamepad. Without it the stick is always the board, and a plain jump stays
  straight. `IA_Pass` moves from LeftShift to X, and the left trigger stops letting the bar out. With
  the modifier held, stick X picks a back roll or a front roll, on the water (pre-wind) and in the
  air. The raley stays physical.

## 1. How tricks reach gameplay today

### 1.1 Input actions and bindings

All actions come from `scripts/editor/make_input_assets.py`. The pawn binds them in
`AKiteRiderPawn::SetupPlayerInputComponent` (`KiteRiderPawn.cpp:421-501`).

| Action | Keyboard | Gamepad | Used when | Script | Pawn binding |
| --- | --- | --- | --- | --- | --- |
| `IA_Steer` | Left / Right | Right stick X | always | `:120-122` | `:429-430` |
| `IA_Sheet` | Down / Up | Right stick Y (negated), **RT**, **LT** (negated: bar out) | always; unhooked, arm extension | `:125-131` | `:434-435` |
| `IA_Edge` | A / D | Left stick X | by state (1.2) | `:137-139` | `:439-440` |
| `IA_WeightShift` | W / S | Left stick Y | by state (1.2) | `:141-143` | `:444-445` |
| `IA_Jump` | Space | A (bottom face) | held on the water: load; let go: pop; pressed in the air: tuck | `:145-146` | `:454-455` |
| `IA_Pause` | Esc | Start | | `:148-149` | `:449`; also `BindKey` Esc, P, Start `:493-495` |
| `IA_Reset` | R | B (right face) | | `:151-152` | `:459`; also `BindKey` R, B and View (`Gamepad_Special_Left`) `:498-500` |
| `IA_RecenterMotion` | Home | R3 | motion bar | `:156-157` | `:463` |
| `IA_GrabFront` / `IA_GrabBack` | Q / E | LB / RB | in the air; both: board-off | `:159-162` | `:468-474` |
| `IA_OneFoot` | C | L3 | in the air | `:163-164` | `:478-479` |
| `IA_Hook` | F | Y (top face) | on the water | `:167-168` | `:484` |
| `IA_Pass` | **LeftShift** | X (left face) | unhooked in the air; on the water, the stance turn (T3.5) | `:169-170` | `:488` |
| Mouse bar | right mouse button held | | | | `KiteRiderPawn.cpp:955` |

**What is free.**

- **Keyboard:** X, Z, V, G, T, Tab, LeftCtrl, RightShift and the number row are free. No
  `Config/DefaultInput.ini` key bindings exist. LeftShift is taken by `IA_Pass`.
- **Gamepad, while riding:**
  - Every trigger, bumper, stick click and face button is taken.
  - The D-pad has no in-ride binding. Only the menu navigator reads it
    (`UI/KiteSurfMenuNavigator.cpp:12-27`), but it sits under the left thumb, which is on the stick.
  - The View button only duplicates reset (`KiteRiderPawn.cpp:500`).
  - LT's bar-out duplicates the right stick pushed forward, and with the bar spring on, letting go of
    RT already returns the bar to the middle (`KiteRiderPawn.cpp:767-801`).

**Against the research's sim-cade scheme.** `docs/research.md:343-354`:

- **Triggers:** the research has RT sheet in and LT "Load edge; release to pop" (`:349-350`). The
  game has both triggers on `IA_Sheet`, and A loads and pops.
- **A and the right stick:** the send and the load therefore both need the right thumb (the right
  stick steers, A loads). Problem 6 below.
- **Left stick in the air, rotations (`:352`):** as designed.
- **Bumpers, grabs (`:353`):** as designed.

### 1.2 How the pawn reads the rider stick (T1.4, #87)

The handlers store the stick in `PlayerRiderStick`: X from `OnEdgeTriggered` (`:1005-1012`), Y from
`OnWeightShiftTriggered` (`:855-862`). They then call `RoutePlayerRiderInput` (`:902-948`). `Tick`
calls it again every frame (`:1940-1944`). The comment there reads: "a held stick moves from the
board to the pre-wind as the load starts, to the air stick at the take-off ... with no new input
event".

`RoutePlayerRiderInput` sends the stick to one of three places, by state:

- **On the water, not loading:** `EdgeBoard(X)` and `SetWeightShift(Y)` (`:945-946`).
- **Loading** (`IsLoadHeld()` on the water):
  - `PreWindStick` = the stick, turned to rotation axes by `ScreenBackSign` (`:921`, `:942`).
  - The board keeps the carve and weight shift it had when the load started
    (`bPreWindLatchesBoardInput`, default true at `:238`; the condition is at `:943`).
- **In the air:** `AirRotationStick` = the stick (`:926`), and `GrabZoneStick` = the stick
  (`:931-934`). Jump pressed in the air sets the tuck (`:928`, `:1014-1023`).

`ScreenBackSign` (`:864-900`) makes "towards the side of the screen the rider's back is on" +X, a back
roll. It is latched at the start of the load.

### 1.3 How input becomes rotation (T1.2 PR E #79, T1.4 #87, T3.2/T3.3 #106)

**Each fixed step.** `StepSimulation` (`:1147-1203`) runs the kite, then:

1. the line force;
2. `StepFreestyleHands` (`:1379-1440`);
3. `StepBar`;
4. `StepGrabs`;
5. `StepRiderAttitude` (`:1570-1680`);
6. `StepBoard`;
7. `StepStance`;
8. the tracker (`:1192-1197`).

**The pre-wind** (`StepRiderAttitude:1603-1621`). While the load is held and
`|PreWindStick| > PreWindStickThreshold` (0.3, `:240`), `PreWindAmount` grows by
`Dt / PreWindBuildSeconds` (0.5 s, `:239`). The direction is the last one held. Neither decays while
the load is held, and both are cleared only when the rider stands up without leaving the water.

**The air stick** (`:1637-1644`). `bRotationInput = |AirRotationStick| > AirRotationDeadzone`
(0.15, `:241`). A grab that is picking its zone zeroes it.

**`URiderAttitudeComponent::Step`** (`Tricks/RiderAttitudeComponent.cpp:405-554`):

- **Take-off, `BeginAir`** (`:179-231`):
  - `L` is zeroed (`:197`).
  - A pre-wind becomes a target rate on the axis `ChooseAxis` picks (`:202-205`): 250 deg/s for a
    roll, 260 for a flip and 360 for a spin (`:40-42`), times the amount, times
    `0.5 + 0.5 * load`.
  - Hooked, a flip is scaled by `HookedFlipScale` 0.35 (`:206-210`).
  - With the raley's arms, a roll pre-wind turns about the lines instead: the S-bend (`:211-222`).
  - The axis is committed (`:227`), so posture damping does not slow the rotation.
- **Line torque** (`:459-468`): `r x F` scaled by `LineTorqueScale` 0.1 at the hook while hooked, or
  by `HandsLineTorqueScale` 1.4 at `LineAttach::AttachPointBody` (`:602-631`) while unhooked.
- **Air control** (`:470-505`):
  - It is a capped torque towards `|stick| * full rate`. The cap is
    `AirControlFractionPerS` 0.3 of a full pre-wind per second.
  - The axis comes from `DefaultRollAxisTiltDeg` (65) if the jump took off rotating, else from
    `AirStickTiltWithoutPreWindDeg` (0, `:46`, `:473`), so stick X alone in the air is a flat spin.
  - With raley arms it turns about the lines (`:482-490`).
- **Landing assist** (`:297-342`): a PD towards upright within `AssistWindowSeconds` 1.0 s of contact
  and `AssistMaxErrorDeg` 90. It is off whenever `bRotationInput` is set (`:323`).
- **Travel align** (`:344-371`): yaw only, and off with input or a committed axis (`:346`).
- **Unhooked extras (#106), in `StepFreestyleHands`:**
  - The flip's arms come in (`:1397`) and the tantrum's back hand comes off (`:1419`).
  - Both are driven by `IsFlipPreWind` (`:1355-1378`), so they follow the pre-wind.
  - The raley comes from `HasRaleyArms` (`:1350-1353`): unhooked, both hands on the bar and the arm
    extension at least `RaleyArmExtension` 0.85. It is pure line torque; no stick is involved.

### 1.4 Recognition

The tracker:

- it is stepped after the board (`KiteRiderPawn.cpp:1192-1197`);
- it polls the board, kite, grab state and bar (`TrickTrackerComponent.cpp:51-102`);
- it steps `FJumpSession` (`:105`).

The recorder runs two recognisers:

- `FRotationRecognizer` (`JumpRecorder.cpp:122`, `:165`; finished at `:264`). An inversion is body Up
  below -0.3 after being above +0.3 (`RotationRecognizer.h:82-86`, `:105-134`). Spins are half turns
  of the integral about U, less the flight's turn.
- `FRaleyRecognizer` (`JumpRecorder.cpp:123`, `:166`).

`Finalise` (`JumpRecorder.cpp:244-283`) builds `FJumpRecord`.

### 1.5 Grading

- The board calls `LandingEvaluator::Evaluate` at touchdown (`BoardMovementComponent.cpp:955`,
  `:983`, `:997`). The rules are in `LandingEvaluator.cpp:31-129`.
- The verdict's speed retention is the only gameplay effect of the grade (`:1012-1013`).
- The record's grade is the verdict's: `TrickScoring::GradeFromVerdict` (`JumpRecorder.cpp:274`,
  #90).
- `OnBoardLandingVerdict` is broadcast (`BoardMovementComponent.cpp:1019`, `:1030`) but nothing
  outside tests binds it. The same is true of `OnBoardTakeoff` and `OnBoardApex`.

### 1.6 Naming and scoring

- **In the recorder:**
  - `TrickRecognition::SignatureFromJump` (`JumpRecorder.cpp:268`);
  - `TrickNaming::Name` (`:276`);
  - `TrickScoring::ScoreJump`, which is height to an exponent × (1 + extremity) × (1 + technicality)
    × execution (`:279`; `TrickScoring.cpp:52-139`);
  - the repeat factor (`JumpRecorder.cpp:333`, `:337`).
- **Best-three session:** `FBestThreeSession` (`SessionScoring.cpp:68`, `:72`), polled by
  `UTrickSessionSubsystem::StepSession` (`TrickSessionSubsystem.cpp:86-127`).
  - Started from `kitesurf.Session` (`KiteSurf.cpp:454-469`) or the pause menu
    (`KiteSurfPauseMenuWidget.cpp:186-200`).
  - Saved only when it sets a new best (`TrickSessionSubsystem.cpp:139-142`).
- **Freestyle heat:** `FFreestyleHeat`, which sorts each record (`FreestyleHeat.cpp:28-40`), re-signs
  it (`:45`) and scores it with `FreestyleScoring` (`:52`, `:141`). `UFreestyleHeatSubsystem`
  (`FreestyleHeatSubsystem.cpp:65-126`, `:154-216`) runs it.
  - Started from `kitesurf.Heat` (`KiteSurf.cpp:470-500`) or the pause menu.
  - It refuses to start during a session or a lesson.

### 1.7 What the player sees

`AKiteSurfHUD` polls the tracker every frame:

- **Ticker:** `FormatTrickTicker(GetLiveJump())` while a jump is in progress
  (`KiteSurfHUD.cpp:272-277`, `:310-312`; drawn at `:475-484`). It says "Back roll" as soon as the
  rider inverts.
- **Card:**
  - Built by `FormatJumpCard` (`:214-253`): name, grade, points, landing g, and a cause line.
  - Shown for 4 s, for jumps with an apex of 1 m or more (`:287-327`; drawn at `:485-532`).
- **Session row and results:** `DrawSession` (`:1182-1295`).
- **Heat row, list and results:** `DrawHeat` (`:1413-1563`).
- **No sound, haptic, camera or particle effect** reacts to the grade, the name or a rotation:
  - the landing splash and haptic follow the landing g only (`KiteRiderPawn.cpp:2984-2994`,
    `:519-525`);
  - the haptics that do exist are on inputs: pop, grab, hook, pass and bar lost.

### 1.8 What is saved

- **Writing:** `UTrickTrackerComponent::StepTracker` calls `UKiteSurfGameInstance::RecordTrickLanding`
  for every finished jump (`TrickTrackerComponent.cpp:121-127`), which feeds
  `FTrickBook::RecordLanding` (`KiteSurfGameInstance.cpp:113-116`, `TrickBook.cpp:11-49`). Its
  "first landing" return value is ignored.
- **Saving:** the book is in `UKiteSurfSaveGame::TrickBook` (`KiteSurfSaveGame.h:94-95`). It reaches
  disk only when something else calls `SaveSettingsToDisk`. Quitting from the pause menu does not
  save (`KiteSurfPauseMenuWidget.cpp:556-562`).
- **Showing:** the only UI is a count, "TRICK BOOK n tricks" (`KiteSurfSchoolWidget.cpp:664-671`).
- **Stale comments:** `TrickBook.h:61-62` and `KiteSurfGameInstance.h:165-166` still say the book is
  not fed by the game.

### 1.9 Wired against tests-only

Every `Tricks/` header has a gameplay caller:

- the attitude (`KiteRiderPawn.cpp:236`);
- the bar, grabs and board-off (`:1498`, `:1270`, `:2316`);
- the evaluator (`BoardMovementComponent.cpp:955`);
- the loop tracker (`KiteComponent.cpp:1266`);
- recognition, naming and scoring (section 1.6);
- the session and heat subsystems (section 1.6);
- the book (section 1.8).

What is left over:

- **Functions with no gameplay caller:** `TrickNaming::GrabName` (`TrickNaming.cpp:542`),
  `FJumpRecorder::GetRaley` (`JumpRecorder.h:257`), `FJumpSession::GetTrickSession`
  (`JumpRecorder.h:343`) and `UTrickTrackerComponent::ClearSession` (`TrickTrackerComponent.h:94`).
- **Computed and discarded:** `TrickScoring::GradeLanding` runs inside `SignatureFromJump`, and the
  verdict overwrites its result (`JumpRecorder.cpp:274`).
- **Different settings:** the ticker re-signs the live jump with default settings
  (`KiteSurfHUD.cpp:275`), not the recorder's (`JumpRecorder.cpp:268`).
- **Not checked on a real ride:** `RecognisesMegaloopBackRoll` is still the synthetic test
  (`docs/tricks/README.md` status row for #88).
- **No `-game` check recorded for rotations:** none for #79, #87, #88 or #106. The T1 exit check was
  never reported (`docs/tricks.md:728`, `docs/tricks/T1.md:668-676`). The one rotation note from a
  `-game` run is a crash: the pre-wind alone under-rotates on the 9 m kite in 24 kn
  (`docs/jumping.md:204`).

## 2. Why the rider rolls on every jump

### The path, step by step

1. **The player is taught to hold the rider stick through the take-off.**
   - `README.md:222`: "Weight on the tail (S) digs the rail in and loads the pop".
   - School B1 (`School/LessonCatalog.cpp:413`): "Carve: edge hard upwind" on `IA_Edge`.
   - B1 (`:420`): "Carve and stomp: pop".
   - B2's fault (`:449`): "Hold the edge until take-off".
2. **Jump is pressed with the stick held.**
   - `OnJumpPressed` (`KiteRiderPawn.cpp:1014-1023`) routes the stick, and the board is now
     loading.
   - `PreWindStick` becomes the stick (`:942`). Any stick past 0.3 counts, including a gentle carve.
   - The board's carve and weight shift freeze (`:943-947`), so carving harder while loading no
     longer carves.
3. **Every fixed step of the load the pre-wind builds** (`:1608-1614`).
   - It is full after 0.5 s; a timed send holds the load for about 0.9 s.
   - Letting go of the stick does not lose it (`:1603-1605`).
4. **The take-off turns it into a committed rotation** (`RiderAttitudeComponent.cpp:202-230`).
   Which rotation depends on what was held:

   | Held | `ChooseAxis` result | Rate |
   | --- | --- | --- |
   | A carve (X): towards the rider's back, which is upwind when riding heelside | back roll | 250 deg/s × (0.5 + 0.5 × load) |
   | A carve the other way | front roll | same |
   | The tail weight (S, stick Y = -1) | backflip (inside the 20° flip sector) | 0.35 × 260 deg/s hooked (`:206-210`) |
   | Both | roll, axis tilted 97° | roll rate |

5. **The stick still held after the pop becomes the rotation stick.**
   - `Tick` re-routes it (`:1940-1944`) to `AirRotationStick` (`:926`), which sets `bRotationInput`
     (`:1638`).
   - The control torque then drives the roll (`RiderAttitudeComponent.cpp:474-505`). On a jump that
     left with no pre-wind, it spins the rider flat (`:46`, `:473`).
   - The same flag turns off the landing assist (`:323`) and the travel align (`:346`), the two
     things that would have kept a plain jump straight.

### What it produces *(log)*

- **Hooked flip pre-wind (the S case):** `KiteSurf.Trick.HookedFlipUnderRotates` gives "Hooked:
  'Backflip' (Crash, cause Inverted) ... pitch 137 deg". This is a scripted pull-Y pre-wind, which is
  what S held through the load produces.
- **Stick X held in the air:** `KiteSurf.Input.AirSpinReachable` turns the rider 416 deg about up.
- **A back-roll pre-wind:** `KiteSurf.Trick.BackRollFromPreWindOnRide` gives one inversion at 0.52 s.
  The rider is not upright again until 2.97 s of 4.88 s, and lands "Sketchy TooHard". The HUD
  ticker says "Back roll" as the rider inverts (`KiteSurf.Trick.CardNamesBackRoll`).

### What does not cause it

- **No default rotation:** with no pre-wind the take-off leaves `L` at zero (`:197`, `:223`).
- **The hooked line torque does not roll a rider.** It leans the body back about 31 deg
  (`KiteSurf.Trick.LineTorqueHangsRiderBack`). With no input the timed jump never inverts and tilts
  at most 22 deg *(log, `NoInputNoRotationOnRide`)*.
- **Travel align** is yaw only.
- **Unhooked, two rotations are physical and need no stick:**
  - the raley, with the bar's arm extension at 0.85 or more at the pop;
  - the roll from one hand on the bar during an unhooked grab (`KiteSurf.Trick.OneHandAttachRolls`).

### What the design documents said

- `docs/tricks.md:360`: "A pop with no pre-wind gives none".
- `docs/tricks.md:413-414` defined the pre-wind as "Left stick held while loading" and the left stick
  in the air as the rotation.
- `docs/research.md:352` has "Left stick in the air: Rotations" in every scheme.
- `docs/tricks/T1.md:52-54` saw the clash with carving and chose to freeze the board ("Gameplay
  change: no carving while loaded").
- `docs/tricks/README.md:48-51`, decision 7: "There is no new `IA_Rotate`".
- `docs/jumping.md:153` records the trap as a feature: "Holding S for the tail weight as the load
  starts already winds a backflip".

So the code does what the plan said. The plan assumed a player who does not want a trick leaves the
stick in the middle, and the game's own school and README teach the opposite. The school's planned
back roll lesson takes the same view: "Carve about 90° upwind before leaving the water"
(`docs/tutorials.md:149`).

### Why the suite did not catch it

- The player-path helper sets S, presses jump, and overwrites the stick in the same frame, before
  any step can build a pre-wind (`Tests/TrickInputTests.cpp:241-244`).
- The ride tests set the board's weight shift directly and call the scripted `SetPreWind`
  (`Tests/TrickRotationTests.cpp:215-229`).
- So no test holds S or a carve through a load without wanting a trick.

## 3. Ranked problems

| # | Problem | Kind | Evidence | Against |
| --- | --- | --- | --- | --- |
| 1 | **Rotation fires on ordinary jumps.** The board's stick is the pre-wind while loading and the rotation stick in the air. A plain jump with the stick held for carving or tail weight leaves the water rolling or flipping, and the held stick also switches off the landing assist. | Bug (by design) | Section 2 | The user's report; `tricks.md:360` |
| 2 | **The rotation's result is set by line tension, not by the player.** | Feel | See the notes below the table | `tricks.md` 6.1 (calibrated "1.5 to 2.5 s"); section 8 |
| 3 | **A hooked flip always under-rotates and crashes Inverted.** S, the tail weight, starts one. | Feel / bug | `HookedFlipScale` 0.35 (`RiderAttitudeComponent.cpp:59`, `:206-210`); *(log)* "Backflip (Crash, cause Inverted) ... pitch 137 deg" | T3.3 intended hooked flips to be weak, not fatal |
| 4 | **Every big jump is graded SKETCHY "Landed too hard: redirect the kite".** | Feel / tuning | See the notes below the table | `tricks.md` 6.7; physics phase 3 entry 38 (the redirect assist was not committed) |
| 5 | **Nothing but the HUD text reacts to a trick.** No stomp rumble, rotation whoosh, grab snap or camera response. | Integration gap | `OnBoardLandingVerdict` has no gameplay listener (section 1.5); feedback follows g only (section 1.7) | `tricks.md` 6.9 (`:685-699`) |
| 6 | **Gamepad: loading and sending need the same thumb.** A loads and pops; the right stick sends. | Input | `make_input_assets.py:122`, `:146`; `research.md:350` puts the load on LT for this reason | `research.md` control schemes |
| 7 | **Trick book:** not saved when a trick is first landed, no list to look at, no "new trick" notice, stale comments. | Integration gap | Section 1.8 | T2.7, backlog G4 |
| 8 | **The same air stick gives different tricks** depending on the take-off: stick X is a roll after a pre-wind and a flat spin without one. | Design | `RiderAttitudeComponent.cpp:46`, `:473`; `jumping.md:155` | `tricks.md:414` ("X alone: roll") |
| 9 | **Rotations were never checked in a `-game` run.** The one recorded run crashed. | Verification | Section 1.9; `jumping.md:204` | `AGENTS.md` "Say what was verified"; T1 exit check |
| 10 | **Two unhooked automatic rotations.** Pushing the bar out unhooked (arm extension at least 0.85) makes any pop a raley. A one-hand grab rolls the body. Both are physical, but could surprise a player. | Feel (watch) | `KiteRiderPawn.cpp:1350-1353`; `HandsLineTorqueScale` 1.4; `OneHandAttachRolls` | T3.2, T3.3 |
| 11 | **Odds and ends:** unused functions, the discarded `GradeLanding`, the ticker's default settings, the synthetic megaloop test. | Cleanup | Section 1.9 | |
| 12 | **The docs encode problem 1:** decision 7, `tricks.md` 6.3, `jumping.md:145-158`, the legend rows (`KiteSurfControlsLegend.cpp:19-21`). | Docs | | |

Notes on problem 2:

- On the timed 30 kn jump a full back roll inverts at 0.52 s, then the lines hold the rider 60 to
  80 deg off upright until the assist pulls them round in the last second (`jumping.md:162`;
  *(log)* "upright again 2.97 s" of 4.88 s).
- At 3.5 kN (the 9 m kite in 24 kn) the same pre-wind stops at horizontal and the rider crashes
  under-rotated (`jumping.md:204`). The hooked line torque grows linearly with tension
  (`RiderAttitudeComponent.cpp:459-468`).
- Holding the air stick for 0.15 s lands the roll; holding it for 0.3 s over-rotates and crashes
  (`jumping.md:158`; `TrickInputTests.cpp:521-523`).
- The player has no "stop": the assist only acts with the stick released and within 90 deg of
  upright (`:323-325`).

Notes on problem 4:

- *(log)* The straight air and the back roll on the 30 kn timed jump both land "Sketchy TooHard" at
  4.26 g. The card reads "Straight air SKETCHY 7 pts / 4.3 g landing / Landed too hard: redirect the
  kite".
- The g is under `SketchyMinLandingG` 8, so the trigger must be the raw sink being over
  `HotLandingSinkMS` 6 m/s (`LandingEvaluator.cpp:95-104`, `LandingEvaluator.h:139`). This is
  inferred from the code; the sink is not in the log.

## 4. Plan in batches

Each batch is one pull request against `main`, following `AGENTS.md`: build, run
`scripts/run-tests.sh -nullrhi`, read the `Test Results:` line, wait for CI, squash-merge. Every batch
ends with a `-game` check by the user, reported separately from the test result. New tests go in one
file per batch (`docs/tricks/README.md` decision 1).

### Batch A (first): rotation only with a held modifier

**What it fixes.** Problem 1, problem 8, and the docs in problem 12. Problem 3 is reduced: a hooked
flip can then only happen on purpose.

#### The binding

| Option | Verdict |
| --- | --- |
| **Keyboard: LeftShift** (the user's choice) | **Use it.** The left pinky holds it while the fingers are on WASD and the thumb is on Space. It is taken by `IA_Pass`, so the pass moves to **X**, which is free and matches the pad's X (left face) for the same action. |
| **Gamepad: LT** (the user's "a trigger") | **Use it, as a digital press past half travel.** It is the left index finger, on the hand that holds the board stick, so "modifier plus direction" is one hand. Its current job, letting the bar out, is already done by the right stick pushed forward, and by letting go of RT with the bar spring on. The research's "LT = load edge" (`research.md:350`) was never built; A loads. |
| Bumper (LB / RB) | No. They are the grabs, and both together are the board-off; the stick picks the zone while one is held. Making a bumper the modifier breaks grabbed rotations. |
| Stick click (L3 / R3) | No. L3 is the one-footer, and holding a stick click while pushing that stick is imprecise and tiring. R3 is the motion bar recentre. |
| Hold jump in the air | No. On the water jump is already held for every load, so it cannot gate the pre-wind, and that is where problem 1 comes from. In the air it is the tuck. On a pad it is also the right thumb, which steers the kite. |

**Cost of LT.**

- Pad players lose "half a trigger: half way out" (`README.md:65`, `movement.md:71`).
- Unhooked, the arms go out (the raley's 0.85) with the right stick forward instead.
- Problem 6 (moving the load off A) can then not use LT. Batch E has to solve it another way.

#### Implementation

1. **Input asset** (`scripts/editor/make_input_assets.py`):
   - Create `IA_Rotate` (Boolean) after the `IA_Hook` / `IA_Pass` loop (`:97-105`).
   - Add the mappings `(ia_rotate, 'LeftShift')` and `(ia_rotate, 'Gamepad_LeftTriggerAxis')`, the
     latter with an `unreal.InputTriggerDown` whose `actuation_threshold` is 0.5. With no trigger,
     Enhanced Input fires on any non-zero value (engine `EnhancedPlayerInput.cpp:241`). If that
     property is awkward to set from Python, map the digital `Gamepad_LeftTrigger` instead.
   - Change `(ia_pass, 'LeftShift')` to `(ia_pass, 'X')` (`:169`).
   - Delete `(ia_sheet, 'Gamepad_LeftTriggerAxis', True)` (`:131`).
   - Set `cdo_rider.set_editor_property('rotate_action', ia_rotate)` beside `pass_action` (`:212`).
2. **Pawn header** (`KiteRiderPawn.h`):
   - `TObjectPtr<UInputAction> RotateAction` beside `PassAction` (`:1055`), and `GetRotateAction()`
     beside `:162`.
   - Public `OnRotatePressed` / `OnRotateReleased` beside `OnJumpPressed` (`:229-232`), so tests
     drive the player path.
   - `bool IsRotateHeld() const`.
   - A tunable `RotateReleaseGraceSeconds`, 0.1 s, *estimate*.
3. **Binding:** in `SetupPlayerInputComponent`, bind Started and Completed beside the pass (`:486-489`).
4. **Routing** (`RoutePlayerRiderInput`, `KiteRiderPawn.cpp:902-948`):
   - **In the air:** `AirRotationStick = bRotateHeld ? Rotation : 0` (`:926`). `GrabZoneStick`
     still takes the stick whatever the modifier (`:931-934`), so grabs and board-off variants are
     unchanged.
   - **On the water:** `PreWindStick = (bLoading && bRotateHeld) ? Rotation : 0` (`:942`).
   - **The latch:** freeze the board only while both are held:
     `if (!(bLoading && bRotateHeld) || !bPreWindLatchesBoardInput)` (`:943`). So the tail weight
     and the carve keep working through a plain load, as the school teaches.
   - **The tick:** the re-routing in `Tick` (`:1940-1944`) stays, and is now gated.
5. **Pre-wind** (`StepRiderAttitude:1606-1621`): on the player path (`bPlayerRiderInput`), clear
   `PreWindAmount` and `PreWindDirection` once the modifier has been up for more than
   `RotateReleaseGraceSeconds` while still on the water. Letting Shift and Space go in the same frame
   then still rolls.
   - The scripted setters stay ungated: `SetPreWind`, `SetAirRotationInput`, `kitesurf.PreWind`
     and `kitesurf.Input` (`:828-838`; `KiteSurf.cpp:85-129`).
   - Tests and menu videos that use them keep working.
6. **Stick mapping** (`URiderAttitudeComponent` constructor, `RiderAttitudeComponent.cpp:43-46`):

   | Tunable | From | To | Effect |
   | --- | --- | --- | --- |
   | `AirStickTiltWithoutPreWindDeg` | 0 | 65 (= `DefaultRollAxisTiltDeg`) | Stick X with the modifier is a back or front roll in the air as well as on the water. The header already describes this setting (`RiderAttitudeComponent.h:374-384`). |
   | `RollAxisTiltRangeDeg` | 45 | 65 | Keeps the spin reachable: a keyboard diagonal is a 19 deg tilt. Spins are needed for back to blind and the pass. |
   | `SpinAxisTiltMaxDeg` | 20 | 25 | With the two above, W + A/D (the diagonal up) is a spin. |

   Resulting mapping:
   - X alone: a roll at 65 deg.
   - Diagonal up: a spin, about 19 deg.
   - Diagonal down: a more inverted roll, about 111 deg.
   - Y alone: a flip.
   - Amount: the stick's size sets the rate (`:205`, `:496`), and on keys the wind-up time does (0.5 s
     to full).
7. **The raley and #106's line-torque tricks.**
   - **The raley stays physical**, with no modifier. It is the lines pulling an unhooked body with
     the arms out (`:459-468`, `HasRaleyArms`). The rider has already chosen it twice: by unhooking
     (Y / F) and by pushing the arms out. A button that changed line physics would be a cheat, not
     an input.
   - **Stick-driven #106 tricks gate on their own.** These are:
     - the S-bend (`:211-222`, `:482-490`);
     - the tantrum and front flip (flip pre-wind, `:206-210`);
     - the flip's arms and the tantrum's hand-off (`IsFlipPreWind`, `KiteRiderPawn.cpp:1355-1378`).

     All of them read the pre-wind or the air stick, so step 4 gates them with no change in the
     attitude.
   - **The one-hand roll on an unhooked grab stays physical.**
8. **Text:**
   - The legend (`UI/KiteSurfControlsLegend.cpp`):
     - `:12`: "Right stick, RT" instead of "triggers".
     - `:19`: "Pre-wind: hold Shift / LT and a direction while loading".
     - `:20`: "Air, with Shift / LT: towards your back: back roll; away: front roll; up-diagonal:
       spin; W / S: flip".
     - `:27`: pass on "X".
   - The `kitesurf.Stick` and `kitesurf.Pass` help text (`KiteSurf.cpp:117`, `:177`).
   - A new `kitesurf.RotateButton <0|1>` beside `kitesurf.JumpButton` (`:211`).
9. **Assets:** build first, because the script sets `rotate_action` on the CDO. Then run
   `scripts/run-python.sh scripts/editor/make_input_assets.py`.
   - This is the headless `-run=pythonscript` commandlet (`scripts/run-python.sh:19`). Input assets
     need no renderer (`.agents/skills/kitesurf-editor-python/SKILL.md` step 4). #97 and #104 added
     their actions the same way.
   - Read the log for the printed lines and for tracebacks.
   - Commit the script with `Content/Input/IA_Rotate.uasset`, `IMC_Default.uasset` and
     `Content/Blueprints/BP_KiteRider.uasset` (LFS). Check that `git status` shows only those
     binaries.
10. **Docs:**
    - `README.md` controls;
    - `docs/jumping.md:145-159`;
    - `docs/tricks.md:410-425`;
    - `docs/movement.md:71`;
    - `docs/tricks/README.md`: amend decision 7 (`:48-51`) to say that `IA_Rotate` is a held gate
      and the stick stays `IA_Edge` / `IA_WeightShift`, so nothing is bound twice; add a status row;
    - `docs/tutorials.md:149`: C1's drill gains "hold Shift / LT".

#### Acceptance tests

New file `Private/Tests/TrickRotateGateTests.cpp`. The player-path tests go through the handlers
(`OnEdgeTriggered`, `OnWeightShiftTriggered`, `OnJumpPressed`, `OnRotatePressed`), with the stick
held across frames as a player holds it.

| Test | What it checks |
| --- | --- |
| `KiteSurf.Trick.PlainJumpStaysStraightWithStickHeld` | The 30 kn timed jump with S and a full carve held from before the load, through the pop and 1 s into the air, no modifier. Pre-wind 0 at the pop. `TookOffRotating()` false. Air rotation input zero. 0 inversions. Rotation about up within 30 deg of the flight's turn. Maximum tilt under 50 deg. Not Crash. The board's weight shift stays -1 and its carve follows the stick through the load. |
| `KiteSurf.Trick.TailWeightIsNotABackflip` | The `HookedFlipUnderRotates` pop and the timed jump, hooked, with S held through the load and no modifier. No Flip family, no inversion, no Inverted crash. |
| `KiteSurf.Trick.RotateHeldPicksBackRoll` | Modifier plus the stick towards the back while loading. Pre-wind 1 at the pop. One inversion, recorded as `BackRoll`. The chest turns to the tail first. Not Crash. |
| `KiteSurf.Trick.RotateHeldPicksFrontRoll` | The same, with the stick away from the back. One inversion, recorded as `FrontRoll`. |
| `KiteSurf.Trick.RotateLetGoBeforePopCancels` | The modifier let go 0.3 s before the pop: no rotation. Let go in the pop's frame: still the roll (the grace). |
| `KiteSurf.Trick.AirStickNeedsRotate` | In the air, stick X without the modifier gives zero air input, and the assist acts in its window. With the modifier pressed in the air, the control torque is non-zero and the family is Roll, not Spin. |
| `KiteSurf.Trick.RotateStickMapping` | Pure, `RiderAxes::ChooseAxisBody` with the new defaults. (1, 0) is a Roll at 65 deg. (0.71, 0.71) is a Spin. (0.71, -0.71) is a Roll over 90 deg. (0, ±1) is a Flip. Half stick gives half the take-off rate, within 5%. |
| `KiteSurf.Trick.RotateAmountFollowsWindUp` | Modifier and a full stick for 0.25 s of the load: pre-wind 0.5 ± 0.05. |
| `KiteSurf.Trick.GrabZoneWithoutRotate` | A grab held picks its zone from the stick, with and without the modifier. |
| `KiteSurf.Trick.RaleyStaysPhysical` | Unhooked pop with the arms out and no modifier: still "Raley", tilt over 60 deg. Stick X without the modifier: no S-bend. With the modifier: the S-bend, as `SBendFromRaleyRoll`. |

Tests to update:

- `KiteSurf.Input.AssetsValid` (`KiteSurfHUDTests.cpp:41-245`):
  - `IA_Rotate` in `TrickActionNames` (`:75`): Boolean, one key and one button;
  - `LeftShift` and LT map to `IA_Rotate`, and `X` maps to `IA_Pass` (`:157-171`);
  - `Gamepad_LeftTriggerAxis` leaves `NegativeKeys`, so "All 6" becomes 5 (`:122`);
  - the CDO's `GetRotateAction()` is `IA_Rotate`.
- In `TrickInputTests.cpp`: `PreWindLatchesBoard`, `AirStickRolls`, `AirSpinReachable` (the spin is
  now the modifier plus the up-diagonal), `TuckWhileJumpHeld`, `BackRollSideFollowsScreen` and
  `LegendShowsAirControls`. `SendAndPop` (`:234-262`) presses the modifier whenever it is given a
  pre-wind.
- The player-stick paths in `TrickGrabTests.cpp:129-130`.
- The comment at `TrickUnhookTests.cpp:872`.

#### Risks

- **Mapping change:** step 6 can change the family of scripted pre-winds that use stick Y. Run the
  whole suite: the stance, pass and raley tests use scripted Y.
- **Muscle memory:** LT no longer lets the bar out, and the pass moves from Shift to X.
- **School:** lessons B1 to B3 are unaffected; they now carve and shift weight as they teach.
  Chapter C, not built yet, must prompt the modifier.
- **`-game` check for the user:**
  1. 30 kn, hold D and S through the load and pop: straight.
  2. Shift + D while loading: a back roll.
  3. Shift + A: a front roll.
  4. On a pad: LT plus the left stick.
  5. Shift + W + D in the air: a spin.

### Batch B: rotations the player controls

**What it fixes.** Problem 2, and the rest of problem 3. This is the biggest feel item once Batch A
has stopped the accidental rolls.

**Decision the user should make first.** `docs/tricks.md` section 9 (`:806-808`) chose "the take-off
decides, air torque capped". This batch moves towards "hold to rotate, let go to finish", the usual
game answer. The rotation still starts at the pop.

**Changes**, all in `RiderAttitudeComponent.cpp` and its header:

1. **Tension-independent rolls while hooked.** While an axis is committed, cap the part of the
   hooked line torque along that axis at `CommittedLineTorqueMaxNm`. Use the 800 N value the bare
   component was calibrated at (`jumping.md:199-204`): about 0.1 × 0.23 m × 800 N, or 19 N·m.
   - The swing towards the lines, off that axis, stays.
   - Do not apply it unhooked: the raley is that torque.
   - Code: `:459-468`.
2. **Holding keeps the rate.** While the modifier and the stick are held, the control cap
   (`:497`) also covers the line torque along the axis, so the target rate is held. How many turns
   then follows how long the player holds.
3. **Letting go finishes forward.** With no rotation input and an axis committed, a capped torque
   along that axis keeps the turn going at no less than `FinishMinRateDegS` (*estimate* 120 deg/s)
   until the body is within `AssistMaxErrorDeg` of the next upright attitude ahead. The existing
   assist (`:297-342`) then lands it, so a rider no longer hangs at 60 to 80 deg or stops at
   horizontal.
4. **No hooked flips.** `HookedFlipScale` goes from 0.35 to 0 (`:59`). Hooked, the flip sector gives
   nothing. Flips stay unhooked tricks (tantrum, front flip).

**Files:**

- `Tricks/RiderAttitudeComponent.h/.cpp`;
- `docs/jumping.md:162` and `:176-204` (the numbers move);
- `docs/tricks.md` 6.1.

**Acceptance tests** (`Private/Tests/TrickRotationControlTests.cpp`):

- `KiteSurf.Trick.BackRollLandsAcrossTension`: the recommended kite at 20, 25, 30 and 35 kn, and the
  9 m kite at 24 kn. One inversion, not Crash, upright before 80% of the airtime.
- `KiteSurf.Trick.HoldRotateCountsRolls`: on a jump with at least 6 s of airtime, holding for
  long enough gives two inversions, and letting go after the first gives one.
- `KiteSurf.Trick.ReleaseFinishesRotationForward`: let go at 200 deg into a roll. The rider lands
  having turned 330 to 390 deg, not 0.
- `KiteSurf.Trick.NoHookedFlip`: hooked, a flip-sector pre-wind gives no rotation.

Tests that must stay green: `NoInputNoRotationOnRide`, `RotationStepRateIndependent`,
`AngularMomentumConserved` and `RaleyFromUnhookedPop`.

**Risk: high.** This is tuning, and it reverses a recorded design choice, so it needs the user's
yes and a `-game` check. Keep each change behind its own tunable so it can be backed out.

### Batch C: landing grades that match the jump

**What it fixes.** Problem 4.

**Change.** Decide "too hard" from the landing g only. The g already includes the 45 cm absorb.
Either drop the raw-sink rule, or raise `HotLandingSinkMS` from 6 to about 9 m/s; the storm landings
at 12 m/s would still be hot. Keep `KiteTooLow`. Code: `LandingEvaluator.cpp:95-104`,
`LandingEvaluator.h:137-147`.

**Goal.** A crouched big air is Clean; one landed with the kite dived is Stomped. Ask the user
whether a big air should need the dive to be Clean. The school's B3 teaches that dive
(`LessonCatalog.cpp:471-478`).

**Files:**

- `Tricks/LandingEvaluator.*`;
- the lesson faults that read the cause (`LessonCatalog.cpp:97`, B2 and B3);
- `docs/jumping.md`;
- `docs/tricks.md` 6.7.

**Acceptance tests:**

- `KiteSurf.Trick.CrouchedBigAirLandsClean`: the 30 kn timed jump, crouched, no dive: Clean.
- `KiteSurf.Trick.DivedBigAirStomps`: the dive in the last second, g ≤ 4: Stomped.
- `KiteSurf.Trick.StormLandingStaysHot`: the 60 kn jump at 9.4 g stays Sketchy, TooHard.
- `KiteSurf.Trick.SinkAloneDoesNotGrade`: pure, the evaluator.

**Risk: medium.**

- School tests that expect TooHard will need re-basing.
- Saved session and heat bests become easier to beat.
- `StompedMaxLandingG` (4.0) sits just under the 4.26 g of the reference jump.

### Batch D: feedback for a landed trick, and the trick book

**What it fixes.** Problems 5 and 7.

**Changes:**

- **Landing verdict:** the pawn binds `OnBoardLandingVerdict` (`BoardMovementComponent.cpp:1019`,
  `:1030`).
  - Stomped: a heavy `PlayHaptic` (`KiteRiderPawn.cpp:503`), a short camera kick and a stomp
    one-shot (see the `game-feel` skill).
  - Sketchy: a light wobble.
  - Crash: as now.
- **Rotation whoosh:** a loop whose pitch and volume follow |ω| (`tricks.md:695-698`). New sounds
  come from `make_sound_assets.py`, following the editor-python rule.
- **New trick:**
  - use `RecordTrickLanding`'s return value (`TrickTrackerComponent.cpp:125`) for a HUD "NEW TRICK:
    Back roll" notice;
  - call `SaveSettingsToDisk` at that point;
  - save on quit as well (`KiteSurfPauseMenuWidget.cpp:560`).
- **Comments:** fix the stale ones (`TrickBook.h:61-62`, `KiteSurfGameInstance.h:165-166`).

**Acceptance tests:**

- `KiteSurf.Trick.StompedLandingRumbles`
- `KiteSurf.Trick.CrashLandingThuds`
- `KiteSurf.Trick.NewTrickNoticeOnce`
- `KiteSurf.Trick.TrickBookSavedOnUnlock`
- `KiteSurf.Trick.RotationWhooshFollowsSpin`

**Risk: low.** The sound assets go through the editor script and are committed with it.

### Batch E: show and teach the rotation; the pad send

**What it fixes.** Batch A's control is invisible until it is taught, and problem 6.

**Changes:**

- **HUD:**
  - a small ROTATE cue while the modifier is held;
  - a pre-wind meter next to the load during a modified load (`GetPreWindAmount`, `KiteRiderPawn.h:198`);
  - the ticker shows the degrees turned so far ("Back roll 240°").
- **School:** C1 and C2 prompts (`docs/tutorials.md:145-151`) when chapter C is built.
- **Pad send (problem 6):** with LT taken by Batch A, offer a setting, LOAD ON LB.
  - LB held on the water loads and pops; in the air LB is still the front grab. Grab buttons do
    nothing on the water today (`docs/jumping.md`, Grabs controls).
  - A stays as the default.
  - Ask the user before building it.

**Acceptance tests:**

- `KiteSurf.Trick.HUDShowsRotateCue`
- `KiteSurf.Trick.PreWindMeterFills`
- `KiteSurf.Trick.PadLoadOnBumper` (only if the setting is chosen): the right stick and LB at once
  through the handlers send and pop.

**Risk:** low for the HUD; medium for the pad layout, which needs the user's choice.

## 5. Handback summary

**Cause.** The left stick is the board, but with jump held it becomes the pre-wind
(`KiteRiderPawn.cpp:942`, built at `:1608-1614`), and in the air the rotation stick (`:926`). `Tick`
re-routes a held stick every frame (`:1940-1944`). The README and the school teach holding S and
the carve through the take-off, so plain jumps leave rolling, or with a hooked backflip that crashes
Inverted *(log)*. The held stick also disables the landing assist (`RiderAttitudeComponent.cpp:323`).
Everything else is wired; 366 of 366 tests pass under `-nullrhi`; nothing was seen in `-game`.

**Batch A:**

- **Action:** `IA_Rotate` (Boolean, held) on LeftShift (`IA_Pass` moves to X) and LT, digital past
  0.5; LT stops letting the bar out. Bumpers are grabs, L3 is the one-footer, and jump is held for
  every load, so none of them works as the modifier.
- **Asset:** add it in `scripts/editor/make_input_assets.py`; build, then run
  `scripts/run-python.sh scripts/editor/make_input_assets.py` (headless). Commit the script with
  `IA_Rotate`, `IMC_Default` and `BP_KiteRider`.
- **Pawn:** `RotateAction`, `OnRotatePressed` / `OnRotateReleased`. `RoutePlayerRiderInput` sends the
  stick to the pre-wind or the air, and freezes the board, only while the modifier is held. The
  pre-wind clears 0.1 s after release on the water. The scripted setters stay ungated.
- **Attitude:** `AirStickTiltWithoutPreWindDeg` 65, `RollAxisTiltRangeDeg` 65, `SpinAxisTiltMaxDeg`
  25: X is a back or front roll, the up-diagonal a spin, Y a flip.
- **Raley:** stays physical; the S-bend and flips read the stick, so they gate on their own.
- **Tests:** `KiteSurf.Trick.PlainJumpStaysStraightWithStickHeld`, `TailWeightIsNotABackflip`,
  `RotateHeldPicksBackRoll`, `RotateHeldPicksFrontRoll`, `RotateLetGoBeforePopCancels`,
  `AirStickNeedsRotate`, `RotateStickMapping`, `RotateAmountFollowsWindUp`, `GrabZoneWithoutRotate`,
  `RaleyStaysPhysical`; update `KiteSurf.Input.AssetsValid` and the six `TrickInputTests.cpp` tests.

**Later:** B, rotation that finishes forward whatever the tension; C, big airs graded Clean; D, grade
feedback and saving the trick book; E, a HUD cue, school prompts and the pad send.
