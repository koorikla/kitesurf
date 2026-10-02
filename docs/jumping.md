# Jumping Mechanics & Hang-Time

This document describes the kite-powered jump mechanics, airborne trajectory, landing evaluation, and crash recovery implemented in `UBoardMovementComponent` and `AKiteRiderPawn`.

## Jump Mechanics Lifecycle

### State Machine
The board lifecycle transitions through four distinct states in `EBoardState`:
1. **Displacement**: Speeds below the planing threshold (< 400 cm/s).
2. **Planing**: Speeds $\ge 400 \text{ cm/s}$ skimming over the surface.
3. **Airborne**: Active pop off the water; vertical hydrodynamics and water drag are disabled while above water surface + 10 cm.
4. **Landing**: Touchdown transition evaluating clean speed retention vs. crash recovery.

### Jump Prerequisites & Trigger
A jump can only be initiated while in the `Planing` state, satisfying:
- **Speed**: Board speed $\ge \text{JumpMinSpeedKnots}$ (8 kn = 411.5 cm/s).
- **Edging**: Edge input magnitude $\ge \text{JumpMinEdgeInput}$ (0.40).

Vertical jump impulse is imparted as:
$$J_z = \text{BaseJumpImpulse} \cdot (1 + \text{TailWeightPopBonus} \cdot \text{tail weight}) + \text{KiteLiftFactor} \cdot \max(0, F_{\text{kite}, z})$$
$$\Delta v_z = \frac{J_z}{m_{\text{eff}}}$$

The legs give about 2.5 m/s, a hop of under a metre. The second term is the edge letting go of whatever the lines were pulling upwards with at that moment; after that the line force keeps acting on the rider as a force, so the height of a jump comes from the kite.

### Loading the edge
- The kite lifts the rider off the water by itself when its upward pull passes `LiftoffWeightFactor` (1.5) times their weight.
- A rider who is edging (turn input, or weight on the tail) holds more: up to `LiftoffWeightFactor + EdgedLiftoffWeightBonus` (4.5) times their weight at full edge. That is what lets the pull build while the kite is steered up.
- Releasing the edge with a pop while the lines are loaded is the big jump. Releasing early gives less; holding on until the kite pulls the rider off the edge loses the pop and the timing, and is far lower.
- Measured with the recommended kite, sending the kite hard and popping at the best moment: about 5 m in 15 kn, 15 m in 30 kn, 25 m in 40 kn. A pop with the kite parked is about 1 m; sending the kite without an edge is about 6 m in 30 kn (`KiteSurf.Jump.TimedReleaseBeatsPop`).

### Airborne Dynamics & Apex Envelope
- The line force continues to act on the rider. A kite kept overhead carries part of their weight on the way down.
- A little slack in the lines does not drop the kite: the canopy keeps flying and takes the slack back up. Only with more than `SlackCollapseCm` of slack is it a loose sheet that falls.
- While airborne (> 10 cm above water surface), water buoyancy and water drag forces are disabled.
- The trajectory is clamped at `MaxJumpHeight` (4000 cm = 40 m), with upward velocity zeroed if the ceiling is reached.

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
| `JumpMinSpeedKnots` | `8.0f` | Minimum board speed required to pop a jump in knots. |
| `JumpMinEdgeInput` | `0.4f` | Minimum edging input magnitude required to pop a jump (0..1). |
| `CleanLandingSpeedRetention` | `0.8f` | Fraction of horizontal velocity retained on clean landing (80%). |
| `CrashDecelDuration` | `0.5f` | Duration in seconds to decelerate to zero upon crash landing. |
| `CrashRespawnDelay` | `1.5f` | Time in seconds before rider respawns upright after crashing. |

## HUD Telemetry
`AKiteSurfHUD` renders:
- Current board state: `Displacement`, `Planing`, `Airborne (<height>m)`, or `Landing (Clean/Crash!)`.
- Jump stats: `Best <height>m` and `Apex <height>m`.
