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
$$J_z = \text{BaseJumpImpulse} + \text{KiteLiftFactor} \cdot \max(0, F_{\text{kite}, z})$$
$$\Delta v_z = \frac{J_z}{m_{\text{eff}}}$$

### Airborne Dynamics & Apex Envelope
- Upward aerodynamic kite line force continues to act on the rider, extending hang time when the kite is flown high in the wind window.
- While airborne (> 10 cm above water surface), water buoyancy and water drag forces are disabled.
- The trajectory is clamped at `MaxJumpHeight` (1200 cm = 12 m), with upward velocity zeroed if the ceiling is reached.
- Under standard conditions (15 kn wind, 9 m$^2$ kite, 15 kn board speed), jump apex reaches 2–6 m with 1.5–3.5 s airtime.

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
| `BaseJumpImpulse` | `35000.0f` | Base pop impulse in kg*cm/s. |
| `KiteLiftFactor` | `0.8f` | Multiplier for upward aerodynamic kite line force in seconds. |
| `MaxJumpHeight` | `1200.0f` | Maximum jump apex height clamp in cm (12 m). |
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
