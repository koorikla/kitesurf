# Jumping Mechanics & Hang-Time

This document describes the kite-powered jump mechanics, airborne trajectory, landing evaluation, and crash recovery implemented in `UBoardMovementComponent` and `AKiteRiderPawn`.

## Jump Mechanics Lifecycle

### State Machine
The board lifecycle transitions through four distinct states in `EBoardState`:
1. **Displacement**: Speeds below the planing threshold (< 400 cm/s).
2. **Planing**: Speeds $\ge 400 \text{ cm/s}$ skimming over the surface.
3. **Airborne**: Active pop off the water, or the lines lifting the rider off; the water's forces are off while above water surface + 10 cm, and the air drags on the rider instead.
4. **Landing**: 0.25 s after a touchdown graded anything but Crash, while the touchdown absorber takes the sink out; or the crash recovery.

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
- The rider rotates in the air with `URiderAttitudeComponent` (see Rider rotation below). The board's full orientation in the air is the strapped board's (`SetAirAttitude`, `GetBoardWorldQuat`); the physics root keeps only its heading. Without the attitude (`bUseRiderAttitude` off) the board keeps the old kinematic air orientation: carve input spins it at `AirSpinRate`, left alone it lines up with the travel, and weight shift pitches it by `AirWeightShiftPitchDeg`.
- The line force continues to act on the rider. The pawn tells the kite when the rider is in the air (`UKiteComponent::SetRiderAirborne`), and with the bar centred the kite's assist then flies it to 12 over them and holds it there (`AirborneZenithGain`, `AirborneZenithMaxHeadingDeg`; 0 gain turns that off), so it carries most of their weight on the way down. Bar over still flies it round the window, and a loop is still the rider's.
- **Loops in the air.** In the air a full bar (`UKiteComponent::AirLoopFullBarThreshold`, 0.85 of the bar as it reaches the kite after the dead time; an estimate) loops the kite the bar's way from wherever it is in the window, not only from 35 deg round on that side as on the water (`LoopClockDeg`). Reversing a full bar mid-loop ends the loop and starts one the other way at once, so half a loop one way and half the other is an S-loop, and a loop against the travel (riding right, the bar to the left) is a contra loop; before, the kite had to be parked 35 deg round on the side the rider came from. A bar under 0.85 still flies the kite across the window, so a partial bar is the redirect, and a full bar already held as the rider leaves the water (the send) flies the kite across as before until it is eased under 0.85 or pulled the other way. On the water nothing changed. From the timed 30 kn jump (9 m, 3.25 s in the air), a full bar to the left 0.5 s after take-off flies a whole loop in 1.8 s that the jump record names "Contra loop" (to the right, 2.2 s, "Kiteloop"); 240 deg one way and then about 225 the other is named "S-loop" (`KiteSurf.Kite.AirLoopFromAnyClock`, `KiteSurf.Kite.AirReverseMakesSLoop`, `KiteSurf.Kite.WaterLoopEntryUnchanged`, `KiteSurf.Kite.AirContraLoopIsContra`).
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
- **The grade** (`LandingEvaluator::Evaluate`, below): from the board's tilt from the water and its yaw off the velocity along the water (either way round), the rider's up, the landing g, the sink, the hot flag and the kite's elevation. In the air with the rider attitude the board and the rider are the attitude's; without it, the root's orientation and an upright rider. `GetLastLandingVerdict()` holds the grade, the cause and the speed kept, `GetLastLandingInputs()` what it was graded on, and `OnBoardLandingVerdict` is broadcast for every landing (before `OnBoardCrash` for a crash). `GetLastLandingAngleDeg()` is the evaluator's yaw.
- **Landed** (any grade but Crash): the rider keeps the grade's share of their horizontal speed (stomped 85%, clean 80%, sketchy 60%); the board is in the Landing state for 0.25 s while the absorber works, then planing or displacement. `WasLastLandingClean()` is true. `OnBoardLanding(LandingG)` is broadcast as before.
- **The timed jump at 30 kn** (crouched from the apex) touches down sinking 8.2 m/s with the kite 55 deg up, over 90 cm: 4.8 g, hot (the sink is over 6 m/s), so graded sketchy (cause too hard) and ridden away with 60% of its speed, where the single clean grade kept 80% (`KiteSurf.Physics.GoodLandingIsThreeToSixG`). That is inside the measured 4.2 to 5.5 g; the sink itself is still above the real 3 to 6 m/s, because the kite loses height in the last two seconds (known gaps). Standing, the same landing is 8.6 g: hot, but landed. Past 10 g it is a crash: a 9.5 m/s sink standing (11.2 g), or 12.7 m/s crouched, about where storm jumps come down (the 0.2 s releases land at 12.1 and 12.6 m/s, 9.4 and 9.9 g).
- **Crash Landing** (a Crash grade: tilt past 50 deg, yaw past 75 deg, the rider inverted, or more than `CrashLandingG`):
  - Speed decelerates linearly to 0 over `CrashDecelDuration` (0.5 s).
  - Rider stays at crash location for `CrashRespawnDelay` (1.0 s), 1.5 s from the crash in all.
  - Rider respawns upright (pitch=0, roll=0, at water level) at 8 kn on the tack they were on (`ResetToTack`), with the kite parked at 45 degrees on that side.
  - The kite keeps flying through the crash, but its pull is spent each step: the scripted stop owns the rider's motion. Until plan-3 item 1 the board saved that pull up and applied it in the first step after the reset, and with the bar in and the kite deep in the window in 25 kn that threw the rider 26 m up at 36 m/s; now they come out of the reset at its 8 kn and stay on the water (`KiteSurf.Physics.CrashedRiderIsNotFlung`).

### Landing grades
`LandingEvaluator::Evaluate` (`Tricks/LandingEvaluator.h`, docs/tricks.md 6.7) grades every landing from a jump; the board crashes only on a Crash grade. The thresholds are **estimates**, every limit inclusive, in `UBoardMovementComponent::LandingThresholds` (its `CrashLandingG` and hot-landing limits come from the board's own properties of those names):

| Grade | Tilt from the water normal | Yaw off velocity (either end) | Other | Speed kept |
| :--- | :--- | :--- | :--- | :--- |
| Stomped | ≤ 15° | ≤ 20° | Kite at 45° or higher, landing g ≤ 4 | 85% |
| Clean | ≤ 30° | ≤ 45° | | 80% |
| Sketchy | ≤ 50° | ≤ 75° | Or a hot landing (kite under 45°, sink over 6 m/s) or over 8 g | 60% |
| Crash | Beyond | Beyond | Or board off, bar lost, pass unfinished, rider inverted, over 10 g | 0 |

The yaw is folded to 0..90°, so a switch landing (tail first) grades the same as a forward one (`KiteSurf.Trick.SwitchLandingIsCleanOnBoard`). Each verdict carries a cause for the failure message: under- or over-rotated (tilt past 50°, by the direction of the spin), sideways, inverted, kite too low, too hard, board not caught, bar lost, pass not finished.

What changed against the single angle test (`MaxLandingAngle` 30° and `CleanLandingSpeedRetention` 0.8, both gone):
- A yaw of 30 to 75° was a crash and is now clean (to 45°) or sketchy (`KiteSurf.Trick.BoardLandingUsesEvaluator`); the 90° of `KiteSurf.Jump.CrashRecovery` still crashes, sideways.
- The board's tilt and the rider's up count: a rider who comes down past 50° of tilt or upside down crashes, under-rotated, over-rotated or inverted (`KiteSurf.Trick.UnderRotatedRollCrashesOnPawn`).
- A straight landing keeps 85% of its speed when stomped (`KiteSurf.Jump.CleanLanding`: 0.5 m/s with the kite up is stomped), 80% clean, and a hot landing 60% (sketchy) where it kept 80%.

## Jump record and trick card
`UTrickTrackerComponent` (`Tricks/TrickTrackerComponent.h`, on the pawn as `GetTrickTracker()`) turns every jump into an `FJumpRecord` with a name, a grade and a score (docs/tricks/T0.md sections 4 and 6). It does not tick: `AKiteRiderPawn::StepSimulation` calls `StepTracker` once per fixed step, right after the board. It reads only public getters of the board and the kite, so it works in tests where no delegate is bound:

- **Take-off**: the board's own count, `GetTakeoffCount()`, with `WasLastTakeoffPopped()` and `GetLastTakeoffTimeSeconds()`. Every take-off goes through the board's `BeginAirborne(bool bPopped)`: the pop (the jump key, or letting go of the load) is popped, the kite lifting the rider off is not. Each also broadcasts `OnBoardTakeoff(bPopped)` for anything bound. A skip off the surface (a kite lift-off that comes down under 50 cm) is a take-off but not a jump, so it opens a live record that is dropped.
- **Apex**: the board's `GetLastJumpApexHeight()`, at `GetCurrentJumpApexTimeSeconds()`, the board time at which the jump was highest. The board broadcasts `OnBoardApex(height)` and counts `GetApexCount()` on the first step that is no longer rising after rising, at a new highest point: once per jump, again only if the kite lifts the rider higher after they started down.
- **Airtime and distance**: the board's `GetLastJumpAirtime()` and `GetLastJumpDistance()`. The landing comes the airtime after the take-off time.
- **Sink, landing g and landing yaw**: the board's own, `GetLastLandingSinkMS()` (relative to the surface), `GetLastLandingG()` (1 + v²/(2 g s) over the absorb distance, which the crouch lengthens; see Landing Evaluation above) and `GetLastLandingAngleDeg()` (the landing evaluator's yaw: the board's axis against its velocity along the water), read when the board's jump count goes up. So the trick card and the landing card show the same g, and so do `OnBoardLanding`, the haptics and the sound. The landing angle is the record's `LandingYawDeg` (it was recorded as 0 before the board exposed it). The timed 30 kn jump flown as `KiteSurf.Trick.TrackerRecordsJump` lands hot (sinking over 6 m/s), so its record is graded **Sketchy**, as the board's verdict is (see Grade below). The straight jumps the tests fly land at 0.0 deg (`KiteSurf.Trick.JumpRecordMatchesTrajectory`).
- **Kite loops**: the kite's own records, `UKiteComponent::GetLoopRecords()` and `GetOpenLoop()`. The kite steps an `FKiteLoopTracker` every fixed step with its own heading turn (`GetLastStepTurnDeg()`, the steering, weathercock and gravity turn of the nose, looping or not), so the records do not depend on `IsLooping` or on the HUD's turn counter. Placing the kite (a reset or a relaunch) cancels the open run; a kite crash ends it as a crashed record.
- **The record**: `FJumpRecorder` finalises it when the board's jump count goes up, landed or crashed; a skip or a reset drops it. The loops are those overlapping take-off to landing, plus an open run of 180° or more. `FJumpSession` applies the repeat factor (a landed family pays 1, 0.75, 0.5, ...), keeps the last 200 records and the session's points.
- **Rider rotation** (T1.6): the rider attitude's `GetBodyQuat()` and `GetAngularVelocity()` (the tracker finds the `URiderAttitudeComponent` on its owner), counted by the recorder's `FRotationRecognizer` (`Tricks/RotationRecognizer.h`, pure) on every step the attitude is simulated. The take-off freezes a frame: U world up, T the horizontal travel, S = U x T, and sigma, the side the rider travels towards (`RiderAxes::TravelSide`). Each step integrates the angular velocity on U, T, S and on the body axes; an inversion is counted when the body's up goes below -0.3 of world up after being above +0.3 (a take-off already tilted past +0.3 does not arm one), a flip when the body-frame rotation since it armed is mostly about Right (backflip about -Right), otherwise a roll, back when that rotation runs along `RiderAxes::BackRollAxisBody(sigma, 65)`. The landing stance: the rider lands facing away when the net heading is more than 90 deg off: the swing-twist of qLand x qTakeoff^-1 about U less the flight's turn, with the projected fronts standing in within a few degrees of a half turn about a horizontal axis. At the take-off the chest is on the kite's side of the travel, so facing away is the chest on the other side. (The kite's own direction is no guide: it is often near the zenith at touchdown.) Facing away lands blind after a backside turn, toeside after a frontside one; after a roll the turn is the roll's own (a back roll is backside by definition, so a back roll landing facing away is back roll to blind, whatever sign the roll carried about U: leaning back, a back roll can turn either way about world up), otherwise the sign of the spin integral against sigma. Spins: with nothing inverted, the rotation about U less the flight's own heading turn (the travel align follows the flight, which the kite turns 60 to 75 deg on the timed jumps), snapped to half turns as floor((|spin| + 45) / 180), and moved one half turn towards the integral when that count's parity disagrees with the landing (odd lands facing away); with an inversion, that integral holds each roll's own turn about U (a default back roll adds 150 to 175 deg), so the spin is the landing's instead, one half turn when the rider lands facing away (back to blind is a back roll plus a backside 180).
- **Grade and landing cause** (decision 6 of `docs/tricks/README.md`, T2.6): the board's `GetLastLandingVerdict()`, read when the jump count goes up. The record's `Grade` is the verdict's grade (`TrickScoring::GradeFromVerdict`: a crashed jump is Crash) and its `LandingCause` the verdict's cause, so the record, the score, the card and the board's own grading agree (`KiteSurf.Trick.CardGradeMatchesVerdict`). The score's execution factor follows the record's grade (stomped 1, clean 0.85, sketchy 0.5). `TrickScoring::GradeLanding`, the record-only shortcut (yaw, g and kite elevation, no tilt, rider or sink), grades only records built without a board, as in the pure recorder tests (`KiteSurf.Trick.RecorderGradesFromVerdict`). Until this, the record was graded by `GradeLanding`, which called the timed 30 kn jump clean where the verdict said sketchy.
- **The trick book**: each finished record goes to `UKiteSurfGameInstance::RecordTrickLanding` when the game has that game instance; the book is saved with the settings, not here.

The record's rotation fields: `bRotationTracked` (the attitude was simulated in the air), `Inversions` (in order), `SpinHalfTurns` and `SpinSense`, `SpinDeg` (what the half turns came from), `LandingStance`, `NetHeadingDeg`, and `RollStartSinceTakeoffSeconds`, when the first inversion's rotation started (the start of the run of steps turning at 90 deg/s or faster that led into it; negative when nothing inverted). `TrickRecognition::SignatureFromJump` puts the rotation into the signature, and the roll start against the yank of the first completed kite or megaloop gives the loop its early or late roll (`LoopRollTiming`, 0.1 s either side), so a back roll inside a megaloop names as "Megaloop back roll", "Early megaloop back roll" or "Late megaloop back roll". `LandingCause` is the verdict's cause.

`GetLiveJump()` is the jump in progress (take-off facts, height and airtime so far, the rotation credited so far) with the loops flown since the take-off.

The HUD shows a **trick card** under the jump readout after every jump of a metre or more, for four seconds:

```
Straight air  CLEAN  14 pts
3.6 g landing
```

The first line is in the grade's colour: STOMPED green, CLEAN white, SKETCHY amber, CRASH red. The points are what the session paid (`Score.Total x RepeatFactor`); a repeat adds `(repeat 75%)`. The grade is the board's landing verdict's. When the verdict named a cause, which it does only for a sketchy landing or a crash, the card gets one more line in the grade's colour, the cause the verdict picked (`AKiteSurfHUD::LandingCauseLine`, T2.6):

```
Straight air  CRASH  0 pts
4.9 g landing
Under-rotated: commit the roll earlier
```

| Cause | Line |
| --- | --- |
| `UnderRotated` | Under-rotated: commit the roll earlier |
| `OverRotated` | Over-rotated: stop the turn sooner |
| `Sideways` | Board sideways at touchdown |
| `Inverted` | Upside down at touchdown |
| `KiteTooLow` | Kite too low at touchdown |
| `TooHard` | Landed too hard: redirect the kite |
| `BoardOff` | Board not caught |
| `BarLost` | Bar lost |
| `PassUnfinished` | Pass not finished |
| `BoardNotAligned` | Board not lined up with the feet |

A stomped or clean card has no cause line, because the verdict names no cause for those. The timed 30 kn jump's verdict is sketchy, too hard (a sink over 6 m/s), and its card now says SKETCHY with the too-hard line; before the grade came from the verdict, the card said CLEAN and hid the cause. While a jump is in the air and has something to name, a **ticker** names it live in the same place: a completed kite loop ("Kiteloop", then "Double kiteloop") and the rotation as it is credited ("Back roll" from the moment the rider is inverted, then "Double back roll"; a flat spin as "Backside 180", then "Backside 360"). Landing replaces it with the card. A scripted pre-wind back roll on the timed jump is "Back roll" in the ticker, the record and the card (`KiteSurf.Trick.CardNamesBackRoll`), the same jump with no rotation input stays "Straight air" (`KiteSurf.Trick.StraightJumpStaysStraightAir`), and a roll coming down 80 deg short crashes with the under-rotated line (`KiteSurf.Trick.UnderRotatedCrashShowsCause`).

`kitesurf.Jumps` logs the session as CSV lines tagged `jumpcsv` in `LogKiteSurf`: index, outcome, name, height (m), airtime (s), distance (m), landing g, peak line tension (N), completed loops, points. `grep -o 'jumpcsv,.*' Saved/Logs/KiteSurf.log | cut -d, -f2-` gives the CSV.

## Rider rotation

`URiderAttitudeComponent` (`Source/KiteSurf/Public/Tricks/`) is the rider's rotation in the air for tricks (T1.2 in `docs/tricks.md`). It is **wired**: `AKiteRiderPawn::StepSimulation` steps it after the line force and before `StepBoard`, every fixed step (`StepRiderAttitude`), and the board flies and lands with it.

- **Inputs** (`FAttitudeInputs`): the board's state at the start of the step, its velocity, this step's line force and taut flag, the height above the water and its normal, the board's vertical acceleration (measured step to step, filtered over `VerticalAccelFilterSeconds`, 0.1 s; the take-off starts it from free fall), the load and edge at the last step on the water (the pop zeroes the board's load), the travel side latched at the take-off, and three controls. On the water it copies the riding pose (`ComputeSlavedBodyQuat`: the stance facing and the riding lean from the simulation's own state, so the take-off is the same at any frame rate).
- **Controls** (T1.4). The left stick and WASD (`IA_Edge` for X, `IA_WeightShift` for Y) are read by what the rider is doing; there is no rotation action of its own (`docs/tricks/README.md` decision 7). The motion bar and the mouse bar fly only the kite, so they work alongside it.

  | Rider is | Left stick / WASD | Jump button |
  | --- | --- | --- |
  | On the water | Carve (X) and weight shift (Y), as always | Press and hold: load |
  | Loading (jump held on the water) | The pre-wind. The board keeps the carve and weight shift it had when the load started (`bPreWindLatchesBoardInput`, on by default), so the stick does not also carve | Let go: pop, with the pre-wind |
  | In the air | The rotation stick: X towards the side of the screen the rider's back is on is a back roll, the other way a front roll; Y pulled (S) a backflip, pushed (W) a front flip; X and Y together tilt a roll's axis, down towards inverted, up towards flat | Press and hold: tuck (spins faster); let go: stretch out |

  - **Pre-wind**: winds up over `PreWindBuildSeconds` (0.5 s) to full while the load is held and the stick is past `PreWindStickThreshold` (0.3); with no stick held there is none. The last direction held is what the take-off uses, so letting go of the stick as you pop does not lose it. Holding S for the tail weight as the load starts already winds a backflip: press S, then jump, then let S go (the tail weight stays, latched).
  - **Screen side**: stick X is turned into the rotation's X by `GetScreenBackSign()`, +1 when the rider's back is on the right of the chase camera's view (`ComputeScreenBackSign`: the horizontal back against the camera's right). It is latched when the load starts, or at a take-off without a load, and held for the whole airtime, so the camera swinging round in the air does not swap the sides (`KiteSurf.Input.BackRollSideFollowsScreen`, on both tacks).
  - **Roll or spin**: a jump that leaves the water rotating (a pre-wind) maps the air stick with the roll's `DefaultRollAxisTiltDeg` (65 deg), so stick X drives the roll the rider is in. A jump that leaves with no pre-wind maps it from `AirStickTiltWithoutPreWindDeg` (0 deg): stick X alone spins the rider flat about their up, in the back roll's sense (a backside spin), the A/D air spin the board had before the attitude; stick Y down tilts that spin towards a roll by up to `RollAxisTiltRangeDeg`, which does not invert, and stick Y alone is still a flip. The air control's torque cap (`AirControlFractionPerS`) makes the stick spin build up: on the timed jump the rider turns 200 deg in 2.6 s and, let go, keeps spinning to 420 deg before the assist lands them straight (`KiteSurf.Input.AirSpinReachable`). From the roll's mapping the spin family was out of reach: stick up as far as the flip sector tilts the axis to 22.7 deg, over `SpinAxisTiltMaxDeg` (20).
  - **Letting go** of the stick lets the landing assist work: it acts only with no stick input.
  - **Tuck**: only a press that starts in the air tucks; the press that loads and pops, or a button still held when the kite lifts the rider off, does not until it is pressed again. The press in the air also sets the board's crouch for the landing, as before.
  - **On the timed jump** (30 kn, the recommended kite, through the player's handlers, `KiteSurf.Input.AirStickRolls`): S, jump, the stick towards the back for the pre-wind, let go to pop with the stick held on for 0.15 s: one inversion, upright again at 2.7 s, landed by the assist. With the pre-wind alone it goes over too; holding the stick on for 0.3 s or more adds enough to over-rotate and crash 2.5 s later, so on this kite the stick in a roll is for a rider short of rotation (the 9 m kite in 24 kn, below), or for braking with the stick the other way.
  - **Scripted path**: `SetPreWind(FVector2D)`, `SetAirRotationInput(FVector2D)` and `SetTuck(float)` set the controls directly in rotation axes (stick X +1 back roll, -1 front roll; Y -1 pulled, a backflip), with no screen-side mapping; calling one, or `ApplyScriptedInput`, takes the controls over from the player's stick until a handler fires again (`IsPlayerRiderInputActive`). From the console: `kitesurf.PreWind <x> <y>` and `kitesurf.Input <steer> <sheet rate> <turn> <weight shift> <raw steer> <air rot x> <air rot y> <tuck>` (scripted), and `kitesurf.Stick <x> <y>` and `kitesurf.JumpButton <0|1>` (the player's left stick and jump button, through the same handlers as the keys).
- **The board**: in the air the board is the strapped board (body times strap offset); the root keeps only its heading, held when the nose points nearly straight up or down. `AirSpinRate`, the auto-align and `AirWeightShiftPitchDeg` are not used while the attitude is live. Off (`bUseRiderAttitude` false, or no component), the board flies as before.
- **What is drawn**: the drawn board (`BoardVisual`) takes the attitude's board between the last two steps, placed so the rider turns about their centre of mass (`ComAboveBoardCm` above the board), and eases back onto the root over `RiderHandoverSeconds` (0.2 s) after the landing; a crash or a reset puts it back at once. The jointed rider takes the attitude's body quaternion in the air: over the same 0.2 s from the take-off, and back after the landing, the torso and the pelvis line blend from the riding pose, so the pelvis starts exactly where the riding pose had it. The drawn crouch lets go no faster than the board's `LoadReleaseRatePerSec`, so the pop (which zeroes the board's load) no longer jumps the pelvis 30 cm in a frame: at 60 fps the pelvis moves at most 7 cm a frame against the root through a back roll, against 5 cm through a straight jump (`KiteSurf.Trick.BoardVisualFollowsAttitude`). The camera ignores all of it: in the air it follows the flight and its pivot sits straight above the root, and it never rolls (measured 0.0000 deg through a roll).
- **On a real jump** (the phase 2 timed jump at 30 kn on the recommended kite, the jump button held through the send with a full back-roll pre-wind, `KiteSurf.Trick.BackRollFromPreWindOnRide`): 4.9 s in the air, the chest turns to the tail first, the rider is inverted 0.52 s after the take-off and upright again at 2.97 s, one inversion; the lines hold them 60 to 80 deg off upright until the last second, where the assist brings them round to land at 4 deg of tilt and 2 deg of yaw (sketchy, because the sink is hot). With no input the same jump never inverts, tilts at most 22 deg, keeps the board along the flight and lands at 2 deg of tilt and 3 deg of yaw (`KiteSurf.Trick.NoInputNoRotationOnRide`). 30, 60 and 120 fps give the same rotation (`KiteSurf.Trick.RotationStepRateIndependent`).

The model, one fixed step (SI inside, cm and kg*cm/s^2 only at the boundary):
- **State**: the body quaternion and the angular momentum L about the centre of mass. The inertia is diagonal in the body frame (Front, Right, Up) and blends from stretched to tucked, so `omega = I^-1 L` and a tuck spins the rider faster with no extra rule.
- **Take-off**: the rotation starts from the kinematic pose on the water, so there is no pop. A pre-wind gives a target rate on the chosen axis: the full rate, times how far the pre-wind was built, times `PreWindLoadFloor + (1 - PreWindLoadFloor) * load`. That axis is committed.
- **Torques**:
  - **Line torque**: `LineTorqueScale * r x F` at the hook (`HookOffsetFromComCm`), and only while the lines are taut. It pulls Up towards the lines and does no work on rotation about them. Hanging still, the rider leans back atan(12/20) = 31 deg.
  - **Air control**: the stick sets a target rate. The torque is capped at `AirControlFractionPerS` of a full pre-wind per second, so the take-off decides most of the rotation.
  - **Landing assist**: a PD towards the nearest valid attitude, which is upright with the board along the travel, either way round. It acts only with no stick input, under `AssistWindowSeconds` from contact and within `AssistMaxErrorDeg`.
  - **Travel align**: with no rotation committed (no pre-wind, no stick held this jump), no stick and the assist not acting, a yaw PD about world up keeps the board pointed along the flight, either end first, as the kinematic auto-align did: the kite drags the flight round by about 60 deg on the timed jump, and without it the board came down 65 deg off its course. It comes in over `StrapSettleSeconds` after the take-off.
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
| `DefaultRollAxisTiltDeg` / `RollAxisTiltRangeDeg` | 65 / 45 deg | A tilt of 65 deg is needed so that a default roll counts as an inversion. The tilt stops at 0 (flat). |
| `AirStickTiltWithoutPreWindDeg` | 0 deg | The air stick's tilt before stick Y on a jump with no pre-wind: 0 makes stick X alone a flat spin. Set it to `DefaultRollAxisTiltDeg` for "X alone is a roll" on every jump. |
| `FlipSectorDeg` / `FlipSectorHysteresisDeg` / `SpinAxisTiltMaxDeg` | 20 / 5 / 20 deg | Stick mapping. |
| `PreWindLoadFloor` | 0.5 | |
| `AirControlFractionPerS` / `AirControlResponseSeconds` | 0.3 1/s / 0.25 s | |
| `PostureDampingPerS` / `PostureMaxTorqueNm` | 3 1/s / 60 N*m | |
| `AirAngularDragPerS` | 0.05 1/s | |
| `AssistStrength` / `AssistWindowSeconds` / `AssistMaxErrorDeg` | 1 / 1.0 s / 90 deg | The plan's 0.7 s and 60 deg missed the rolled rider on a real jump, whom the lines hold 60 to 80 deg off upright until the last second. |
| `AssistNaturalFreqHz` / `AssistDampingRatio` / `AssistMaxTorqueNm` | 1.2 Hz / 0.9 / 120 N*m | |
| `TravelAlignNaturalFreqHz` / `TravelAlignDampingRatio` / `TravelAlignMaxTorqueNm` / `TravelAlignMinSpeedCmS` | 0.8 Hz / 1 / 40 N*m / 200 cm/s | 0 Hz turns it off. |
| `StrapSettleSeconds` / `ComOffsetSettleSeconds` | 0.3 / 0.5 s | |

Every default is an *estimate*.

**Calibration.** `LineTorqueScale` and `PreWindRollRateDegS` were set together on the bare component: a full pre-wind back roll at full load, against 800 N of hang tension pulling straight up. That is the worst case for the line torque, since the roll axis is 65 deg off the line.
- With the plan's starting values (0.15 and 200 deg/s), the rider tips half over and falls back the way they came. That is not a roll.
- Just above the energy needed to get over the top, the duration climbs steeply, past 3 s.
- At 0.1 and 250 deg/s the roll goes round in 1.76 s, and 1.70 to 1.85 s at 720 to 880 N, inside the 1.5 to 2.5 s target (`KiteSurf.Trick.BackRollFromPreWind`).
- On the real timed jump (30 kn, the recommended kite, about 2 kN as the rider leaves the water) the same pre-wind goes over once: inverted at 0.52 s, upright again at 2.97 s (`KiteSurf.Trick.BackRollFromPreWindOnRide`).
- In a `-game` run on the 9 m kite in 24 kn, which pulls 3.5 kN at the pop, the pre-wind alone turns the rider only to horizontal: they come down 60 to 77 deg over and crash, under-rotated. Holding the air stick towards the back roll and tucking for the first second takes them over (inverted with the board above them, landed clean). Whether the line torque should scale less than linearly with the tension, or the roll axis lean towards the lines, is open (a calibration question for the next pass).

## Default Tunable Properties

Exposed in `UBoardMovementComponent` under `UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Jump")`:

| Property | Default Value | Description |
| :--- | :--- | :--- |
| `PopImpulseKgCmPerS` | `21000` | Pop impulse from the legs in kg*cm/s (about 2.5 m/s on 85 kg). |
| `TailWeightPopBonus` | `0.5` | Extra pop with the weight fully on the tail, as a fraction of the pop. |
| `EdgeReleaseSeconds` | `0` | Seconds of the kite's upward line force counted again as an impulse when the edge lets go (s). 0 is physics only; phase 1 used 0.22. |
| `LoadHoldBonus` | `1.5` | Upward pull, in rider weights above their own, that a fully loaded rider hangs on to before the lines lift them off. |
| `RiderDragAreaM2` | `0.7` | Drag area of the rider and board in the air (m^2). |
| `AirSpinRate` | `200` | Board spin in the air at full carve (deg/s); only without the rider attitude. |
| `AirWeightShiftPitchDeg` | `30` | Board pitch at full weight shift in the air (deg); only without the rider attitude. |
| `MaxJumpHeight` | `500000` | Maximum jump apex height clamp in cm (5 km, the cloud base). |
| `LandingThresholds` | see Landing grades | The grades' tilt and yaw limits, the stomp's kite and g, and the speed kept per grade. Replaces `MaxLandingAngle` (30) and `CleanLandingSpeedRetention` (0.8). |
| `LandingAbsorbDistanceCm` / `CrouchAbsorbBonus` | `45` / `1.0` | The distance a touchdown's sink is taken out over (cm), and how much longer a full crouch makes it (fraction). |
| `CrashLandingG` | `10` | A landing harder than this (g) is a crash. |
| `HotLandingSinkMS` / `HotLandingKiteElevationDeg` | `6` / `45` | A landing sinking faster than this (m/s), or with the kite lower than this (deg), is hot. |
| `LoadRatePerSec` / `LoadReleaseRatePerSec` | `2.5` / `6.0` | How fast the crouch builds while the jump button is held, and lets go (1/s). |
| `LoadPopBonus` | `0.6` | Extra pop from a full load, as a fraction. |
| `CrashDecelDuration` | `0.5` | Duration in seconds to decelerate to zero upon crash landing. |
| `CrashRespawnDelay` | `1.0` | Time in seconds after the deceleration before the rider respawns upright (1.5 s in all). |

## HUD Telemetry
`AKiteSurfHUD` renders:
- Current board state: `Displacement`, `Planing`, `Airborne (<height>m)`, or `Landing (Clean/Crash!)` (clean: any grade but Crash).
- Jump stats in the telemetry: best height and distance, and the last jump's.
- The jump readout, top centre: `12.4 m high   35 m far   2.1 s` while the rider is more than a metre up, then `JUMP  14.8 m high   62 m far   4.1 s` for four seconds after it ends, in gold with NEW BEST when it beat the session's best height (`UpdateJumpReadout`). Hops under a metre are not announced.
- The trick card under the readout after a jump, and the trick ticker while a named element is in the air (see Jump record and trick card).
- The landing card, for `LandingCardSeconds` (3 s) after each landing from a jump: `LANDED 4.2 g`, with `HOT` after it for a hot landing (the card orange), or `CRASH 9.2 g` (`FormatLandingCard`, `UpdateLandingCard`, polling `UBoardMovementComponent::GetLandingCount`; `KiteSurf.HUD.LandingCard`). Its g is the trick card's.
