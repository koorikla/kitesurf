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
- The kite answers the send after its steering dead time (0.24 s at the start's 70% sheet), so the release is timed from when the kite starts to move. With the recommended kite (loop model), sending the kite hard and popping at the best moment (release swept in 0.1 s steps): about 7 m in 15 kn, 9 m in 20 kn, 15 m in 30 kn (best 0.8 s after the kite answers) and 20 m in 40 kn. The test's 0.7 s release at 30 kn goes 13.1 m with 3.7 s in the air; a pop with the kite parked is 1.5 m, sending the kite without an edge 6.2 m, letting go at 0.3 s 3.9 m, and holding on to 3 s gets the rider pulled off at 5.6 m (`KiteSurf.Jump.TimedReleaseBeatsPop`).

### Airborne Dynamics & Apex Envelope
- The line force continues to act on the rider. A kite kept overhead carries part of their weight on the way down.
- The air drags on the rider and board, $0.5 \rho C_D A |v_a| v_a$ with `RiderDragAreaM2` (0.7 m^2) and $v_a$ the wind at chest height (`UKiteComponent::RiderWindHeightCm`) minus their velocity, sampled at the board's simulation time.
- A little slack in the lines does not drop the kite: the canopy keeps flying and takes the slack back up. Only with more than `SlackCollapseCm` of slack is it a loose sheet that falls.
- While airborne (> 10 cm above water surface), water buoyancy and water drag forces are disabled.
- The trajectory is clamped at `MaxJumpHeight` (4000 cm = 40 m), with upward velocity zeroed if the ceiling is reached.

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
| `MaxJumpHeight` | `4000` | Maximum jump apex height clamp in cm (40 m). |
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
- Jump stats: `Best <height>m` and `Apex <height>m`.
