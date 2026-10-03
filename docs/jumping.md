# Jumping Mechanics & Hang-Time

This document describes the kite-powered jump mechanics, airborne trajectory, landing evaluation, and crash recovery implemented in `UBoardMovementComponent` and `AKiteRiderPawn`.

## Jump Mechanics Lifecycle

### State Machine
The board lifecycle transitions through four distinct states in `EBoardState`:
1. **Displacement**: Speeds below the planing threshold (< 400 cm/s).
2. **Planing**: Speeds $\ge 400 \text{ cm/s}$ skimming over the surface.
3. **Airborne**: Active pop off the water; vertical hydrodynamics and water drag are disabled while above water surface + 10 cm, and the air drags on the rider instead.
4. **Landing**: Touchdown transition evaluating clean speed retention vs. crash recovery.

### Pop
A rider can always pop while they are up on the board on the water: no edge and no minimum speed are needed. `Jump()` is refused (`NotPlaning`, shown as "Get up on the board first") only in the air, during a crash, or while floating. A part-sunk board gives proportionally less push.

### Load
Holding the jump button (`AKiteRiderPawn::SetLoadHeld`, `UBoardMovementComponent::SetLoadHeld`) puts the rider into a crouch with their weight over the back of the board: the jointed rider's pelvis drops and the knees bend. `GetLoadAmount()` builds to 1 over 0.4 s (`LoadRatePerSec`) and lets go at `LoadReleaseRatePerSec`. While loaded:
- lateral grip rises by up to `LoadGripBonus` (150%), so the board slips downwind less and the lines pull harder. How much depends on where the kite is: an edge resists a pull across the board, not one along it. Riding the 9 m in 20 kn with the kite held at its clock position, slip drops from 90 to 39 cm/s and tension rises from 585 to 677 N (`KiteSurf.Jump.LoadAndRelease`);
- the board is held down as with a full edge (see below);
- the pop that follows is up to `LoadPopBonus` (60%) stronger: 4.6 m/s against 3.1 m/s from a tap.

Letting go of the button pops (`ReleaseLoadAndPop`). If the kite has already pulled the rider off the water, letting go does nothing more.

Vertical jump impulse is imparted as:
$$J_z = \text{PopImpulseKgCmPerS} \cdot (1 + \text{TailWeightPopBonus} \cdot \text{tail weight}) \cdot (1 + \text{LoadPopBonus} \cdot \text{load}) + \text{EdgeReleaseSeconds} \cdot \max(0, F_{\text{kite}, z})$$
$$\Delta v_z = \frac{J_z}{m}$$

The legs give about 2.5 m/s (3.7 m/s with the weight on the tail), a hop of under a metre. The second term is the edge letting go of whatever the lines were pulling upwards with at that moment; after that the line force keeps acting on the rider as a force. In the timed jump at 30 kn the release adds 5.7 m/s to the pop's 3.7: most of the take-off speed is this pseudo-impulse rather than the kite lifting the rider through the climb, which is part of why the hang time is short (below).

### Loading the edge
- The kite lifts the rider off the water by itself when its upward pull passes `LiftoffWeightFactor` (1.5) times their weight.
- A rider who is edging (turn input, weight on the tail, or a loaded crouch) holds more: up to `LiftoffWeightFactor + EdgedLiftoffWeightBonus` (4.5) times their weight at full edge. That is what lets the pull build while the kite is steered up.
- Releasing the edge with a pop while the lines are loaded is the big jump. Releasing early gives less; holding on until the kite pulls the rider off the edge loses the pop and the timing, and is far lower.
- The kite answers the send after its steering dead time (0.24 s at the start's 70% sheet), so the release is timed from when the kite starts to move. With the recommended kite (loop model), sending the kite hard and popping at the best moment (release swept in 0.1 s steps): about 7 m in 15 kn, 9 m in 20 kn, 15 m in 30 kn (best 0.8 s after the kite answers) and 20 m in 40 kn. In a storm the kite loads up at once, so the best moment is 0.3 s after it answers: about 28 m and 170 m downwind in 60 kn on the 3 m kite, 30 m and 260 m in 90 kn on the 2 m (`KiteSurf.Wind.StormIsRideable`). A kite far too big for a storm does not go higher: it barely jumps. The test's 0.7 s release at 30 kn goes 13.1 m with 3.7 s in the air; a pop with the kite parked is 1.5 m, sending the kite without an edge 6.2 m, letting go at 0.3 s 3.9 m, and holding on to 3 s gets the rider pulled off at 5.6 m (`KiteSurf.Jump.TimedReleaseBeatsPop`).

### Airborne Dynamics & Apex Envelope
- The line force continues to act on the rider. A kite kept overhead carries part of their weight on the way down.
- The air drags on the rider and board, $0.5 \rho C_D A |v_a| v_a$ with `RiderDragAreaM2` (0.7 m^2) and $v_a$ the wind at chest height (`UKiteComponent::RiderWindHeightCm`) minus their velocity, sampled at the board's simulation time.
- A little slack in the lines does not drop the kite: the canopy keeps flying and takes the slack back up. Only with more than `SlackCollapseCm` of slack is it a loose sheet that falls.
- While airborne (> 10 cm above water surface), water buoyancy and water drag forces are disabled.
- `MaxBoardSpeedCmS` holds only on the water. In the air the kite carries the rider downwind until the wind they feel, and with it the kite's lift, has dropped: that is what brings a rider lofted in a storm back down.
- The only ceiling is `MaxJumpHeight`, at the cloud base (500000 cm = 5 km); upward velocity is zeroed there. No jump measured comes near it: the highest is about 30 m.
- Distance is measured over the water from take-off (`GetCurrentJumpDistance`, `GetLastJumpDistance`, `GetBestJumpDistance`); `GetJumpCount` goes up when a jump's figures are final.

### Hang time
Effective gravity $8h/t^2$ tells how much of the rider the kite carries: real jumps give 1.5 to 3.5 m/s^2 (the kite carrying 65 to 85% of the rider). The timed jump at 30 kn gives 7.8 m/s^2, 13.1 m in 3.67 s (`KiteSurf.Physics.HangTime`, which logs the jump at 20 Hz). The rider climbs level with the kite and on the way down it is low and to the side, about 19 deg above them, lifting about 80 N of their 830 N. The assist steers by the wind the rider feels, which in the air is dominated by their own climb and fall, so the kite does not get back over the top. This is a known gap; see `docs/physics/CHANGELOG.md`.

### Landing Evaluation & Crash Recovery
Upon re-entering the water surface ($v_z \le 0$ and $z \le z_{\text{water}} + 10 \text{ cm}$):
- **Landing Angle**: Computed between horizontal velocity direction and board heading vector ($0..180^\circ$).
- **Clean Landing** (Angle $\le \text{MaxLandingAngle} = 30^\circ$):
  - Rider retains 80% horizontal speed (`CleanLandingSpeedRetention = 0.80`).
  - $v_z$ resets to 0 and board returns to planing/displacement without interruption.
- **Crash Landing** (Angle $> 30^\circ$):
  - Speed decelerates linearly to 0 over `CrashDecelDuration` (0.5 s).
  - Rider stays at crash location for `CrashRespawnDelay` (1.0 s), 1.5 s from the crash in all.
  - Rider respawns upright (pitch=0, roll=0, at water level) at 8 kn on the tack they were on (`ResetToTack`), with the kite parked at 45 degrees on that side.

### Landing grades (planned)
`LandingEvaluator::Evaluate` (`Tricks/LandingEvaluator.h`, docs/tricks.md 6.7) is written and tested but **not yet called by the board**: the clean-or-crash test above is still what decides a landing. When it is wired in, the board's angle test becomes a grade (thresholds are **estimates**, every limit inclusive):

| Grade | Tilt from the water normal | Yaw off velocity (either end) | Other | Speed kept |
| :--- | :--- | :--- | :--- | :--- |
| Stomped | ≤ 15° | ≤ 20° | Kite at 45° or higher, landing g ≤ 4 | 85% |
| Clean | ≤ 30° | ≤ 45° | | 80% |
| Sketchy | ≤ 50° | ≤ 75° | Or a hot landing (kite under 45°, sink over 6 m/s) or over 8 g | 60% |
| Crash | Beyond | Beyond | Or board off, bar lost, pass unfinished, rider inverted, over 10 g | 0 |

The yaw is folded to 0..90°, so a switch landing (tail first) grades the same as a forward one. Each verdict carries a cause for the failure message: under- or over-rotated (tilt past 50°, by the direction of the spin), sideways, inverted, kite too low, too hard, board not caught, bar lost, pass not finished. Today any yaw over 30° crashes; with the table, 30 to 75° will be clean or sketchy, and the 90° of `KiteSurf.Jump.CrashRecovery` still crashes.

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

Exposed in `UBoardMovementComponent` under `UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Jump")` (the lift-off pair under `Tuning`):

| Property | Default Value | Description |
| :--- | :--- | :--- |
| `PopImpulseKgCmPerS` | `21000` | Pop impulse from the legs in kg*cm/s (about 2.5 m/s on 85 kg). |
| `TailWeightPopBonus` | `0.5` | Extra pop with the weight fully on the tail, as a fraction of the pop. |
| `EdgeReleaseSeconds` | `0.22` | Seconds of the kite's upward line force released into the pop when the edge lets go (s). |
| `LiftoffWeightFactor` | `1.5` | Upward pull, in rider weights, that lifts a flat board off the water. |
| `EdgedLiftoffWeightBonus` | `3.0` | Extra the rider holds down at full edge, in rider weights. |
| `RiderDragAreaM2` | `0.7` | Drag area of the rider and board in the air (m^2). |
| `AirSpinRate` | `200` | Board spin in the air at full carve (deg/s). |
| `AirWeightShiftPitchDeg` | `30` | Board pitch at full weight shift in the air (deg). |
| `MaxJumpHeight` | `500000` | Maximum jump apex height clamp in cm (5 km, the cloud base). |
| `MaxLandingAngle` | `30` | Maximum deviation angle in degrees between velocity and board heading for clean landing. |
| `LoadRatePerSec` / `LoadReleaseRatePerSec` | `2.5` / `6.0` | How fast the crouch builds while the jump button is held, and lets go (1/s). |
| `LoadGripBonus` | `1.5` | Extra lateral grip at full load, as a fraction. |
| `LoadPopBonus` | `0.6` | Extra pop from a full load, as a fraction. |
| `CleanLandingSpeedRetention` | `0.8` | Fraction of horizontal velocity retained on clean landing (80%). |
| `CrashDecelDuration` | `0.5` | Duration in seconds to decelerate to zero upon crash landing. |
| `CrashRespawnDelay` | `1.0` | Time in seconds after the deceleration before the rider respawns upright (1.5 s in all). |

## HUD Telemetry
`AKiteSurfHUD` renders:
- Current board state: `Displacement`, `Planing`, `Airborne (<height>m)`, or `Landing (Clean/Crash!)`.
- Jump stats in the telemetry: best height and distance, and the last jump's.
- The jump readout, top centre: `12.4 m high   35 m far   2.1 s` while the rider is more than a metre up, then `JUMP  14.8 m high   62 m far   4.1 s` for four seconds after it ends, in gold with NEW BEST when it beat the session's best height (`UpdateJumpReadout`). Hops under a metre are not announced.
