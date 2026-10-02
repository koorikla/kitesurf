# Jumping Mechanics & Hang-Time

This document describes the kite-powered jump mechanics, airborne trajectory, landing evaluation, and crash recovery implemented in `UBoardMovementComponent` and `AKiteRiderPawn`.

## Jump Mechanics Lifecycle

### State Machine
The board lifecycle transitions through four distinct states in `EBoardState`:
1. **Displacement**: Speeds below the planing threshold (< 400 cm/s).
2. **Planing**: Speeds $\ge 400 \text{ cm/s}$ skimming over the surface.
3. **Airborne**: Active pop off the water; vertical hydrodynamics and water drag are disabled while above water surface + 10 cm.
4. **Landing**: Touchdown transition evaluating clean speed retention vs. crash recovery.

### Pop
A rider can always pop while they are up on the board on the water: no edge and no minimum speed are needed. `Jump()` is refused (`NotPlaning`, shown as "Get up on the board first") only in the air, during a crash, or while floating. A part-sunk board gives proportionally less push.

### Load
Holding the jump button (`AKiteRiderPawn::SetLoadHeld`, `UBoardMovementComponent::SetLoadHeld`) puts the rider into a crouch with their weight over the back of the board: the jointed rider's pelvis drops and the knees bend. `GetLoadAmount()` builds to 1 over 0.4 s (`LoadRatePerSec`) and lets go at `LoadReleaseRatePerSec`. While loaded:
- lateral grip rises by up to `LoadGripBonus` (150%), so the board slips downwind less and the lines pull harder. How much depends on where the kite is: an edge resists a pull across the board, not one along it. Riding the 9 m in 20 kn, slip drops from 85 to 37 cm/s and tension rises from 556 to 636 N;
- the board is held down as with a full edge (see below);
- the pop that follows is up to `LoadPopBonus` (60%) stronger: 4.6 m/s against 3.1 m/s from a tap.

Letting go of the button pops (`ReleaseLoadAndPop`). If the kite has already pulled the rider off the water, letting go does nothing more.

Vertical jump impulse is imparted as:
$$J_z = \text{BaseJumpImpulse} \cdot (1 + \text{TailWeightPopBonus} \cdot \text{tail weight}) \cdot (1 + \text{LoadPopBonus} \cdot \text{load}) + \text{KiteLiftFactor} \cdot \max(0, F_{\text{kite}, z})$$
$$\Delta v_z = \frac{J_z}{m_{\text{eff}}}$$

The legs give about 2.5 m/s, a hop of under a metre. The second term is the edge letting go of whatever the lines were pulling upwards with at that moment; after that the line force keeps acting on the rider as a force, so the height of a jump comes from the kite.

### Loading the edge
- The kite lifts the rider off the water by itself when its upward pull passes `LiftoffWeightFactor` (1.5) times their weight.
- A rider who is edging (turn input, weight on the tail, or a loaded crouch) holds more: up to `LiftoffWeightFactor + EdgedLiftoffWeightBonus` (4.5) times their weight at full edge. That is what lets the pull build while the kite is steered up.
- Releasing the edge with a pop while the lines are loaded is the big jump. Releasing early gives less; holding on until the kite pulls the rider off the edge loses the pop and the timing, and is far lower.
- Measured with the recommended kite, sending the kite hard and popping at the best moment: about 6 m in 15 kn, 15 m in 30 kn, 20 m in 40 kn, 30 m in 60 kn and 48 m in 90 kn, landing 17, 55, 90, 180 and 300 m downwind. The stronger the wind, the earlier the best moment: 1.6 s after sending the kite in 15 kn, 0.2 s in 90 kn. A pop with the kite parked is about 1 m; sending the kite without an edge is about 6 m in 30 kn (`KiteSurf.Jump.TimedReleaseBeatsPop`).

### Airborne Dynamics & Apex Envelope
- The line force continues to act on the rider. A kite kept overhead carries part of their weight on the way down.
- A little slack in the lines does not drop the kite: the canopy keeps flying and takes the slack back up. Only with more than `SlackCollapseCm` of slack is it a loose sheet that falls.
- While airborne (> 10 cm above water surface), water buoyancy and water drag forces are disabled.
- The wind pushes on a rider in the air (`UBoardMovementComponent::ComputeAirDragForce`, drag area `AirDragAreaM2` 0.6 m^2, from the wind they feel). `MaxBoardSpeed` holds only on the water. Together these carry a rider downwind in a storm until the wind they feel, and with it the kite's lift, has dropped: that is what brings them down, and what stops the kite towing them ever faster.
- The only ceiling is `MaxJumpHeight`, at the cloud base (500000 cm = 5 km); upward velocity is zeroed there. No jump measured comes near it.
- Distance is measured over the water from take-off (`GetCurrentJumpDistance`, `GetLastJumpDistance`, `GetBestJumpDistance`); `GetJumpCount` goes up when a jump's figures are final.

### Landing Evaluation & Crash Recovery
Upon re-entering the water surface ($v_z \le 0$ and $z \le z_{\text{water}} + 10 \text{ cm}$):
- **Landing Angle**: Computed between horizontal velocity direction and board heading vector ($0..180^\circ$).
- **Clean Landing** (Angle $\le \text{MaxLandingAngle} = 30^\circ$):
  - Rider retains 80% horizontal speed (`CleanLandingSpeedRetention = 0.80`).
  - $v_z$ resets to 0 and board returns to planing/displacement without interruption.
- **Crash Landing** (Angle $> 30^\circ$):
  - Speed decelerates linearly to 0 over `CrashDecelDuration` (0.5 s).
  - Rider stays at crash location for `CrashRespawnDelay` (1.5 s).
  - Rider respawns upright (pitch=0, roll=0, at water level) with zero velocity, resetting state to `Displacement`.

## Default Tunable Properties

Exposed in `UBoardMovementComponent` under `UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Jump")`:

| Property | Default Value | Description |
| :--- | :--- | :--- |
| `BaseJumpImpulse` | `21000.0f` | Pop impulse from the legs in kg*cm/s (about 2.5 m/s). |
| `KiteLiftFactor` | `0.22f` | Seconds of the kite's upward line force released into the pop. |
| `LiftoffWeightFactor` | `1.5f` | Upward pull, in rider weights, that lifts a flat board off the water. |
| `EdgedLiftoffWeightBonus` | `3.0f` | Extra the rider holds down at full edge. |
| `MaxJumpHeight` | `4000.0f` | Maximum jump apex height clamp in cm (40 m). |
| `MaxLandingAngle` | `30.0f` | Maximum deviation angle in degrees between velocity and board heading for clean landing. |
| `LoadRatePerSec` / `LoadReleaseRatePerSec` | `2.5f` / `6.0f` | How fast the crouch builds while the jump button is held, and lets go. |
| `LoadGripBonus` | `1.5f` | Extra lateral grip at full load, as a fraction. |
| `LoadPopBonus` | `0.6f` | Extra pop from a full load, as a fraction. |
| `CleanLandingSpeedRetention` | `0.8f` | Fraction of horizontal velocity retained on clean landing (80%). |
| `CrashDecelDuration` | `0.5f` | Duration in seconds to decelerate to zero upon crash landing. |
| `CrashRespawnDelay` | `1.5f` | Time in seconds before rider respawns upright after crashing. |

## HUD Telemetry
`AKiteSurfHUD` renders:
- Current board state: `Displacement`, `Planing`, `Airborne (<height>m)`, or `Landing (Clean/Crash!)`.
- Jump stats in the telemetry: best height and distance, and the last jump's.
- The jump readout, top centre: `12.4 m high   35 m far   2.1 s` while the rider is more than a metre up, then `JUMP  14.8 m high   62 m far   4.1 s` for four seconds after it ends, in gold with NEW BEST when it beat the session's best height (`UpdateJumpReadout`). Hops under a metre are not announced.
