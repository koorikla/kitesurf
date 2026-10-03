# Jumping Mechanics & Hang-Time

This document describes the kite-powered jump mechanics, airborne trajectory, landing evaluation, and crash recovery implemented in `UBoardMovementComponent` and `AKiteRiderPawn`.

## Jump Mechanics Lifecycle

### State Machine
The board lifecycle transitions through four distinct states in `EBoardState`:
1. **Displacement**: Speeds below the planing threshold (< 400 cm/s).
2. **Planing**: Speeds $\ge 400 \text{ cm/s}$ skimming over the surface.
3. **Airborne**: Active pop off the water, or the lines lifting the rider off; the water's forces are off while above water surface + 10 cm, and the air drags on the rider instead.
4. **Landing**: 0.25 s after a clean touchdown, while the touchdown absorber takes the sink out; or the crash recovery.

### Pop
A rider can always pop while they are up on the board on the water: no edge and no minimum speed are needed. `Jump()` is refused (`NotPlaning`, shown as "Get up on the board first") only in the air, during a crash, or while floating. A part-sunk board gives proportionally less push.

### Load
Holding the jump button (`AKiteRiderPawn::SetLoadHeld`, `UBoardMovementComponent::SetLoadHeld`) puts the rider into a crouch with their weight over the back of the board: the jointed rider's pelvis drops and the knees bend. `GetLoadAmount()` builds to 1 over 0.4 s (`LoadRatePerSec`) and lets go at `LoadReleaseRatePerSec`. While loaded:
- the board heels `LoadExtraHeelDeg` (25 deg) past the balance of the pull across it (up to `MaxHeelDeg`, 65), so the water's normal force pushes it to windward of its heading, the apparent wind goes aft, the kite sits deeper and the lines pull harder. How much depends on where the kite is: an edge resists a pull across the board, not one along it. Riding the 9 m in 20 kn with the kite held at its clock position, the heel goes from 32 to 59 deg, the board moves 61 cm/s to windward and tension rises from 541 to 672 N (`KiteSurf.Jump.LoadAndRelease`); on the 12 m in 15 kn 1.5 s of load raises it from 448 to 605 N (`KiteSurf.Physics.LoadingBuildsTension`);
- the rider hangs on against the lines: up to `LoadHoldBonus` (1.5) more body weights of upward pull, their legs holding the board on the water (see below);
- the pop that follows is up to `LoadPopBonus` (60%) stronger: 4.0 m/s against 2.5 m/s from a tap.

Letting go of the button pops (`ReleaseLoadAndPop`). If the kite has already pulled the rider off the water, letting go does nothing more.

Held in the air, the button is the crouch for the landing: the load builds there too, changes nothing in the air, and lengthens the touchdown's absorb distance by `CrouchAbsorbBonus` (a full crouch doubles it; see Landing below). Let go of it on the water and it pops again, unless the board is still in its 0.25 s landing state.

Vertical jump impulse is imparted as:
$$J_z = \text{PopImpulseKgCmPerS} \cdot (1 + \text{TailWeightPopBonus} \cdot \text{tail weight}) \cdot (1 + \text{LoadPopBonus} \cdot \text{load}) + \text{EdgeReleaseSeconds} \cdot \max(0, F_{\text{kite}, z})$$
$$\Delta v_z = \frac{J_z}{m}$$

The legs give about 2.5 m/s (3.7 m/s with the weight on the tail, 5.9 m/s from a full load with the weight on the tail), a hop of under a metre. The second term is off by default (`EdgeReleaseSeconds` 0, `docs/physics/plan-2.md` item 2, A3): once the rider is off the water the lines' pull acts on them as a force, and that is all the kite adds. At phase 1 it was 0.22 s of the upward pull counted again as an impulse, which gave the timed jump at 30 kn 5.7 of its 10 m/s of take-off speed; set it back for that feel.

### Loading the edge
- The kite lifts the rider off the water by itself when its upward pull passes their weight.
- A loaded rider (the jump button held) hangs on to more: the board leaves the water when the upward pull passes $m g (1 + \text{LoadHoldBonus} \cdot \text{load})$, 2.5 times their weight at full load (`docs/physics/plan-2.md` item 3; research take-off at 2.5 to 4 body weights of tension). That is what lets the pull build while the kite is steered up. Until then the legs hold the board on the water: 20 cm above its ride height they hold it down with up to `LoadHoldBonus` times the load times their weight and stop it rising there (since plan-2 item 4; it was the 20 cm water-contact clamp). An edge alone (the turn input or the weight on the tail) no longer holds them down (`KiteSurf.Jump.EdgeHoldsRiderDown`).
- Once the lines lift more than the rider weighs the board carries no weight, the water's normal force goes and only the fins and the rail hold the edge (weight on the tail buries the rail, `TailWeightRailScale`): the longer the hold, the more the board slides towards the kite.
- Releasing the edge with a pop while the lines are loaded is the big jump. Releasing early gives less; holding on until the kite pulls the rider off the edge loses the pop and the timing, and is far lower.
- The kite answers the send after its steering dead time (0.24 s at the start's 70% sheet), so the release is timed from when the kite starts to move. With the recommended kite (loop model), sending the kite hard with the jump button held and the weight on the tail, letting go at the best moment and crouching again for the landing: about 4.4 m in 15 kn (1.74 s after the kite answers; landed at 2.6 g), 10.7 m in 30 kn (0.66 s; from 0.70 s the lines pull the rider off first, at 2.5 body weights; landed at 4.8 g, hot) and 15.3 m in 40 kn (0.44 s; landed at 6.9 g, hot; from 0.46 s the lines pull the rider off first, for 10.1 m). Those are crouched landings; standing the same touchdowns are 4.3, 8.6 and 12.8 g, the last a crash. The test's 0.66 s release at 30 kn goes 10.7 m with 5.0 s in the air; 0.64 s gives 9.9 m. A pop with the kite parked is 1.0 m, sending the kite without an edge 3.0 m, letting go at 0.3 s 2.4 m, and holding on to 3 s gets the rider pulled off at 4.9 m (`KiteSurf.Jump.TimedReleaseBeatsPop`).
- In a storm the kite loads up at once, so the best moment is 0.2 s after it answers; from 0.22 s in 90 kn (0.3 s in 60 kn) the lines pluck the loaded rider off first, lower. Let go at 0.2 s, that is 21.2 m and 156 m downwind in 60 kn on the 3 m kite and 22.4 m and 225 m in 90 kn on the 2 m (`KiteSurf.Wind.StormIsRideable`), and both land crouched, sinking 12.1 and 12.6 m/s: 9.4 and 9.9 g, hot, just inside the crash (the descent gap above). The higher storm jumps come down harder and crash: in 60 kn every release from 0.22 s until the pluck, the 0.25 s jump (23.9 m) at 13.4 m/s and 11.2 g and the highest (24.5 m at 0.28 s) at 11.4 g; in 90 kn the releases at 0.17 to 0.19 s, which go highest (25.0 m at 0.18 s), at 11.1 g. Height still levels off above 60 kn (`docs/physics/storm-jumps.md`). A kite far too big for a storm does not go higher: it barely jumps.

### Airborne Dynamics & Apex Envelope
- The line force continues to act on the rider. The pawn tells the kite when the rider is in the air (`UKiteComponent::SetRiderAirborne`), and with the bar centred the kite's assist then flies it to 12 over them and holds it there (`AirborneZenithGain`, `AirborneZenithMaxHeadingDeg`; 0 gain turns that off), so it carries most of their weight on the way down. Bar over still flies it round the window, and a loop is still the rider's.
- The air drags on the rider and board, $0.5 \rho C_D A |v_a| v_a$ with `RiderDragAreaM2` (0.7 m^2) and $v_a$ the wind at chest height (`UKiteComponent::RiderWindHeightCm`) minus their velocity, sampled at the board's simulation time.
- A little slack in the lines does not drop the kite: the canopy keeps flying and takes the slack back up. Only with more than `SlackCollapseCm` of slack is it a loose sheet that falls.
- While airborne (> 10 cm above water surface), water buoyancy and water drag forces are disabled.
- `MaxBoardSpeedCmS` holds only on the water. In the air the kite carries the rider downwind until the wind they feel, and with it the kite's lift, has dropped: that is what brings a rider lofted in a storm back down.
- The only ceiling is `MaxJumpHeight`, at the cloud base (500000 cm = 5 km); upward velocity is zeroed there. No jump measured comes near it: the highest is about 25 m, in a 60 to 90 kn storm (24.5 m in 60 kn, 25.0 m in 90 kn).
- Distance is measured over the water from take-off (`GetCurrentJumpDistance`, `GetLastJumpDistance`, `GetBestJumpDistance`); `GetJumpCount` goes up when a jump's figures are final.

### Hang time
Effective gravity $8h/t^2$ tells how much of the rider the kite carries: real jumps give 1.5 to 3.5 m/s^2 (the kite carrying 65 to 85% of the rider). The timed jump at 30 kn gives 3.46 m/s^2, 10.7 m in 4.98 s (`KiteSurf.Physics.HangTime`, which logs the jump at 20 Hz): the lines hold up 0.70 of the rider's weight over the flight, and on the way down the kite is about 71 deg above them, flying unstalled (`KiteSurf.Physics.KiteOverheadInTheAir`). It is above 60 deg 2.1 s after take-off. Over the last 1.75 s it sinks from 84 to 55 deg above the rider and their sink grows to 8.2 m/s at touchdown, where the research has 3 to 6 m/s under a kite held overhead: a known gap (`docs/physics/CHANGELOG.md`). At phase 1 the assist steered by the wind the rider feels, which in the air is dominated by their own climb and fall, and the same jump gave 7.8 m/s^2. A loop flown from the apex is a known gap: it peaks at about 0.9 body weights with the kite still 60 deg up, against the research's 3 to 5 with the kite low (`KiteSurf.Physics.AirborneLoopYanks`).

A redirect before landing (`docs/physics/plan-3.md` item 2: the assist flying the kite from 12 forward and down on the travel side one to two seconds before touchdown) was tried and is not in the game. With the recommended 6 m kite in 30 kn it brings the timed jump's sink down from 8.2 to 5.6 m/s at best, but only with the kite down at 53 to 57 deg at touchdown; with the kite kept above 60 deg the best is 7.2 m/s, about what a gentler overhead hold gives (7.3 to 7.4 m/s with `AirborneZenithMaxHeadingDeg` 20). The turn costs pull before the kite's speed adds any (it stalls in the turn), and in a storm, where the rider is carried downwind with the wind, the kite flown to the side loses its vertical pull and every variant made the landing harder (`docs/physics/CHANGELOG.md`, Phase 3). Bar centred in the air still holds the kite overhead all the way down.

### Landing Evaluation & Crash Recovery
Upon coming down to the water (sinking into it relative to its surface, which on a swell may be rising, and within 10 cm of it; `docs/physics/plan-2.md` item 4, `docs/physics/research.md` 3.4):
- **The sink** $v$ (m/s, relative to the surface) is taken out by the touchdown absorber at a constant $v^2 / (2 s)$ over the absorb distance $s$: `LandingAbsorbDistanceCm` (45 cm standing: the legs, the board's immersion and the water's give; research 0.2 to 0.4 m of legs and immersion) times $1 + \text{CrouchAbsorbBonus} \cdot \text{crouch}$ (1: a full crouch doubles it). The board goes $s$ on into the water and comes back up on the buoyancy and the planing lift; nothing is snapped or zeroed.
- **Landing g**: $1 + v^2 / (2 g s)$, what `OnBoardLanding` reports, `GetLastLandingG()` holds (with `GetLastLandingSinkMS()` and `GetLastLandingAbsorbCm()`) and the HUD's landing card shows. Standing, 2 m/s is 1.7 g, 4 m/s 3.7 g and 6 m/s 7.1 g (the ratio of the squares); crouched 1.3 and 4.0 g; at 6 m/s the water's push on the board peaks at the landing's g and the board goes 31 cm on into the water (`KiteSurf.Physics.LandingGFromSink`).
- **Hot** (`WasLastLandingHot()`): sinking faster than `HotLandingSinkMS` (6 m/s), or with the kite under `HotLandingKiteElevationDeg` (45 deg) above the rider. A flag, not a crash.
- **Landing Angle**: between the horizontal velocity and the board's axis, either way round ($0..90^\circ$).
- **Clean Landing** (Angle $\le$ `MaxLandingAngle` (30 deg) and the landing g at most `CrashLandingG` (10)):
  - Rider retains 80% horizontal speed (`CleanLandingSpeedRetention = 0.80`).
  - The board is in the Landing state for 0.25 s while the absorber works, then planing or displacement.
- **The timed jump at 30 kn** (crouched from the apex) touches down sinking 8.2 m/s with the kite 55 deg up, over 90 cm: 4.8 g, hot (the sink is over 6 m/s), ridden away (`KiteSurf.Physics.GoodLandingIsThreeToSixG`). That is inside the measured 4.2 to 5.5 g; the sink itself is still above the real 3 to 6 m/s, because the kite loses height in the last two seconds (known gaps). Standing, the same landing is 8.6 g: hot, but landed. Past 10 g it is a crash: a 9.5 m/s sink standing (11.2 g), or 12.7 m/s crouched, about where storm jumps come down (the 0.2 s releases land at 12.1 and 12.6 m/s, 9.4 and 9.9 g).
- **Crash Landing** (Angle $> 30^\circ$, or more than `CrashLandingG`):
  - Speed decelerates linearly to 0 over `CrashDecelDuration` (0.5 s).
  - Rider stays at crash location for `CrashRespawnDelay` (1.0 s), 1.5 s from the crash in all.
  - Rider respawns upright (pitch=0, roll=0, at water level) at 8 kn on the tack they were on (`ResetToTack`), with the kite parked at 45 degrees on that side.
  - The kite keeps flying through the crash, but its pull is spent each step: the scripted stop owns the rider's motion. Until plan-3 item 1 the board saved that pull up and applied it in the first step after the reset, and with the bar in and the kite deep in the window in 25 kn that threw the rider 26 m up at 36 m/s; now they come out of the reset at its 8 kn and stay on the water (`KiteSurf.Physics.CrashedRiderIsNotFlung`).

### Landing grades (planned)
`LandingEvaluator::Evaluate` (`Tricks/LandingEvaluator.h`, docs/tricks.md 6.7) is written and tested but **not yet called by the board**: the clean-or-crash test above is still what decides a landing. When it is wired in, the board's angle test becomes a grade (thresholds are **estimates**, every limit inclusive):

| Grade | Tilt from the water normal | Yaw off velocity (either end) | Other | Speed kept |
| :--- | :--- | :--- | :--- | :--- |
| Stomped | ≤ 15° | ≤ 20° | Kite at 45° or higher, landing g ≤ 4 | 85% |
| Clean | ≤ 30° | ≤ 45° | | 80% |
| Sketchy | ≤ 50° | ≤ 75° | Or a hot landing (kite under 45°, sink over 6 m/s) or over 8 g | 60% |
| Crash | Beyond | Beyond | Or board off, bar lost, pass unfinished, rider inverted, over 10 g | 0 |

The yaw is folded to 0..90°, so a switch landing (tail first) grades the same as a forward one. Each verdict carries a cause for the failure message: under- or over-rotated (tilt past 50°, by the direction of the spin), sideways, inverted, kite too low, too hard, board not caught, bar lost, pass not finished. Today any yaw over 30° crashes; with the table, 30 to 75° will be clean or sketchy, and the 90° of `KiteSurf.Jump.CrashRecovery` still crashes.

## Jump record and trick card
`UTrickTrackerComponent` (`Tricks/TrickTrackerComponent.h`, on the pawn as `GetTrickTracker()`) turns every jump into an `FJumpRecord` with a name, a grade and a score (docs/tricks/T0.md sections 4 and 6). It does not tick: `AKiteRiderPawn::StepSimulation` calls `StepTracker` once per fixed step, right after the board. It reads only public getters of the board and the kite, so it works in tests where no delegate is bound:

- **Take-off**: the board entering `Airborne`. The take-off time is the board time less `GetCurrentJumpAirtime()`. It counts as **popped** when the board already has airtime on the first step in the air: a pop (the jump key, or letting go of the load) leaves the water between steps, a kite lift-off inside one. This is an inference until the board exposes `WasLastTakeoffPopped` (physics phase 2).
- **Apex**: the board's `GetLastJumpApexHeight()`, at the time of the step the board was seen highest.
- **Airtime and distance**: the board's `GetLastJumpAirtime()` and `GetLastJumpDistance()`.
- **Sink and landing g**: the board's own, `GetLastLandingSinkMS()` (relative to the surface) and `GetLastLandingG()` (1 + v²/(2 g s) over the absorb distance, which the crouch lengthens; see Landing Evaluation above), read when the board's jump count goes up. So the trick card and the landing card show the same g, and so do `OnBoardLanding`, the haptics and the sound. The timed 30 kn jump flown as `KiteSurf.Trick.TrackerRecordsJump` flies it lands at 7.4 g, graded **Clean**: the grade reads the g and the kite's elevation, not the board's hot flag. The landing yaw is not exposed by the board, so it is recorded as 0.
- **Kite loops**: an `FKiteLoopTracker` stepped with the kite's heading turn each step: the signed angle between successive `GetKiteHeading()` values about the line from the rider to the kite (positive to the kite's right, the sign of `GetTurnDeg`). In the tests it agrees with the kite's own turn count to within 1%. A heading jump over 45° in one step, or a reset, cancels the open run. T0.3's kite hookup replaces this with the kite's own per-step turn.
- **The record**: `FJumpRecorder` finalises it when the board's jump count goes up, landed or crashed; a skip or a reset drops it. The loops are those overlapping take-off to landing, plus an open run of 180° or more. `FJumpSession` applies the repeat factor (a landed family pays 1, 0.75, 0.5, ...), keeps the last 200 records and the session's points.
- **The trick book**: each finished record goes to `UKiteSurfGameInstance::RecordTrickLanding` when the game has that game instance; the book is saved with the settings, not here.

`GetLiveJump()` is the jump in progress (take-off facts, height and airtime so far) with the loops flown since the take-off.

The HUD shows a **trick card** under the jump readout after every jump of a metre or more, for four seconds:

```
Straight air  CLEAN  14 pts
7.4 g landing
```

The first line is in the grade's colour: STOMPED green, CLEAN white, SKETCHY amber, CRASH red. The points are what the session paid (`Score.Total x RepeatFactor`); a repeat adds `(repeat 75%)`. While a jump is in the air and has something to name, today a completed kite loop, a **ticker** names it live in the same place ("Kiteloop", then "Double kiteloop"). Landing replaces it with the card.

`kitesurf.Jumps` logs the session as CSV lines tagged `jumpcsv` in `LogKiteSurf`: index, outcome, name, height (m), airtime (s), distance (m), landing g, peak line tension (N), completed loops, points. `grep -o 'jumpcsv,.*' Saved/Logs/KiteSurf.log | cut -d, -f2-` gives the CSV.

## Rider rotation (not yet wired)

`URiderAttitudeComponent` (`Source/KiteSurf/Public/Tricks/`) is the rider's rotation in the air for tricks (T1.2 in `docs/tricks.md`). **The pawn does not step it yet**: the air orientation in the game is still `AirSpinRate` and the auto-align above. Wiring it into `StepSimulation`, after the line force and before `StepBoard`, comes in a later change. Until then it runs only in the `KiteSurf.Trick.*` tests.

The model, one fixed step (SI inside, cm and kg*cm/s^2 only at the boundary):
- **State**: the body quaternion and the angular momentum L about the centre of mass. The inertia is diagonal in the body frame (Front, Right, Up) and blends from stretched to tucked, so `omega = I^-1 L` and a tuck spins the rider faster with no extra rule.
- **Take-off**: the rotation starts from the kinematic pose on the water, so there is no pop. A pre-wind gives a target rate on the chosen axis: the full rate, times how far the pre-wind was built, times `PreWindLoadFloor + (1 - PreWindLoadFloor) * load`. That axis is committed.
- **Torques**:
  - **Line torque**: `LineTorqueScale * r x F` at the hook (`HookOffsetFromComCm`), and only while the lines are taut. It pulls Up towards the lines and does no work on rotation about them. Hanging still, the rider leans back atan(12/20) = 31 deg.
  - **Air control**: the stick sets a target rate. The torque is capped at `AirControlFractionPerS` of a full pre-wind per second, so the take-off decides most of the rotation.
  - **Landing assist**: a PD towards the nearest valid attitude, which is upright with the board along the travel, either way round. It acts only with no stick input, under `AssistWindowSeconds` from contact and within `AssistMaxErrorDeg`.
- **Posture damping and drag**: posture damping decays rotation off the committed axis, or all rotation when no axis is committed; drag acts on every axis. Both are exact exponential decays, so they cannot overshoot at any step size.
- **Rotation**: the free rigid-body motion for the step, exact for a symmetric top. With no torque, |L| and the energy are conserved.
- **Board**: the strapped board is the body times a strap offset. The offset eases from the take-off heel and pitch to flat under the feet.

The tunables are under the category `Tuning|Rotation`:

| Property | Default | Notes |
| :--- | :--- | :--- |
| `InertiaStretchedKgM2` / `InertiaTuckedKgM2` | (13, 13, 2) / (7, 7, 1.5) kg*m^2 | Front, Right, Up; rider plus board. |
| `TuckSmoothSeconds` | 0.15 s | Critically damped tuck. |
| `HookOffsetFromComCm` | (12, 0, 20) cm | From the centre of mass, not the pelvis (`HarnessHookOffsetCm`). |
| `LineTorqueScale` | 0.1 | Calibrated: see below. |
| `PreWindRollRateDegS` / `PreWindFlipRateDegS` / `PreWindSpinRateDegS` | 250 / 260 / 360 deg/s | Full pre-wind at full load. |
| `DefaultRollAxisTiltDeg` / `RollAxisTiltRangeDeg` | 65 / 45 deg | A tilt of 65 deg is needed so that a default roll counts as an inversion. |
| `FlipSectorDeg` / `FlipSectorHysteresisDeg` / `SpinAxisTiltMaxDeg` | 20 / 5 / 20 deg | Stick mapping. |
| `PreWindLoadFloor` | 0.5 | |
| `AirControlFractionPerS` / `AirControlResponseSeconds` | 0.3 1/s / 0.25 s | |
| `PostureDampingPerS` / `PostureMaxTorqueNm` | 3 1/s / 60 N*m | |
| `AirAngularDragPerS` | 0.05 1/s | |
| `AssistStrength` / `AssistWindowSeconds` / `AssistMaxErrorDeg` | 1 / 0.7 s / 60 deg | |
| `AssistNaturalFreqHz` / `AssistDampingRatio` / `AssistMaxTorqueNm` | 1.2 Hz / 0.9 / 120 N*m | |
| `StrapSettleSeconds` / `ComOffsetSettleSeconds` | 0.3 / 0.5 s | |

Every default is an *estimate*.

**Calibration.** `LineTorqueScale` and `PreWindRollRateDegS` were set together on the bare component: a full pre-wind back roll at full load, against 800 N of hang tension pulling straight up. That is the worst case for the line torque, since the roll axis is 65 deg off the line.
- With the plan's starting values (0.15 and 200 deg/s), the rider tips half over and falls back the way they came. That is not a roll.
- Just above the energy needed to get over the top, the duration climbs steeply, past 3 s.
- At 0.1 and 250 deg/s the roll goes round in 1.76 s, and 1.70 to 1.85 s at 720 to 880 N, inside the 1.5 to 2.5 s target (`KiteSurf.Trick.BackRollFromPreWind`).
- Repeat the calibration on a real jump once the attitude is wired, and again once the kite sits overhead in the air (physics phase 2).

## Default Tunable Properties

Exposed in `UBoardMovementComponent` under `UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Jump")`:

| Property | Default Value | Description |
| :--- | :--- | :--- |
| `PopImpulseKgCmPerS` | `21000` | Pop impulse from the legs in kg*cm/s (about 2.5 m/s on 85 kg). |
| `TailWeightPopBonus` | `0.5` | Extra pop with the weight fully on the tail, as a fraction of the pop. |
| `EdgeReleaseSeconds` | `0` | Seconds of the kite's upward line force counted again as an impulse when the edge lets go (s). 0 is physics only; phase 1 used 0.22. |
| `LoadHoldBonus` | `1.5` | Upward pull, in rider weights above their own, that a fully loaded rider hangs on to before the lines lift them off. |
| `RiderDragAreaM2` | `0.7` | Drag area of the rider and board in the air (m^2). |
| `AirSpinRate` | `200` | Board spin in the air at full carve (deg/s). |
| `AirWeightShiftPitchDeg` | `30` | Board pitch at full weight shift in the air (deg). |
| `MaxJumpHeight` | `500000` | Maximum jump apex height clamp in cm (5 km, the cloud base). |
| `MaxLandingAngle` | `30` | Maximum deviation angle in degrees between velocity and board heading for clean landing. |
| `LandingAbsorbDistanceCm` / `CrouchAbsorbBonus` | `45` / `1.0` | The distance a touchdown's sink is taken out over (cm), and how much longer a full crouch makes it (fraction). |
| `CrashLandingG` | `10` | A landing harder than this (g) is a crash. |
| `HotLandingSinkMS` / `HotLandingKiteElevationDeg` | `6` / `45` | A landing sinking faster than this (m/s), or with the kite lower than this (deg), is hot. |
| `LoadRatePerSec` / `LoadReleaseRatePerSec` | `2.5` / `6.0` | How fast the crouch builds while the jump button is held, and lets go (1/s). |
| `LoadPopBonus` | `0.6` | Extra pop from a full load, as a fraction. |
| `CleanLandingSpeedRetention` | `0.8` | Fraction of horizontal velocity retained on clean landing (80%). |
| `CrashDecelDuration` | `0.5` | Duration in seconds to decelerate to zero upon crash landing. |
| `CrashRespawnDelay` | `1.0` | Time in seconds after the deceleration before the rider respawns upright (1.5 s in all). |

## HUD Telemetry
`AKiteSurfHUD` renders:
- Current board state: `Displacement`, `Planing`, `Airborne (<height>m)`, or `Landing (Clean/Crash!)`.
- Jump stats in the telemetry: best height and distance, and the last jump's.
- The jump readout, top centre: `12.4 m high   35 m far   2.1 s` while the rider is more than a metre up, then `JUMP  14.8 m high   62 m far   4.1 s` for four seconds after it ends, in gold with NEW BEST when it beat the session's best height (`UpdateJumpReadout`). Hops under a metre are not announced.
- The trick card under the readout after a jump, and the trick ticker while a named element is in the air (see Jump record and trick card).
- The landing card, for `LandingCardSeconds` (3 s) after each landing from a jump: `LANDED 4.2 g`, with `HOT` after it for a hot landing (the card orange), or `CRASH 9.2 g` (`FormatLandingCard`, `UpdateLandingCard`, polling `UBoardMovementComponent::GetLandingCount`; `KiteSurf.HUD.LandingCard`). Its g is the trick card's.
