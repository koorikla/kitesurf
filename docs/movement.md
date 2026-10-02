# Movement Mechanics & Hydrodynamics

This document outlines the board movement, edging, planing transition, and physics tuning constants implemented in `UBoardMovementComponent` and `AKiteRiderPawn`.

## Physics Architecture

### Dual Drag Regimes
1. **Displacement Regime** ($v < 400 \text{ cm/s}$):
   - Quadratic drag: $F_{quad} = C_{disp} \cdot v^2$
   - Linear drag: $F_{lin} = C_{disp,lin} \cdot v$
   - Rapid deceleration when unpowered: bringing rider from displacement speeds to $< 2$ knots within 5 seconds.
2. **Planing Regime** ($v \ge 400 \text{ cm/s}$):
   - Linear drag: $F_{lin} = C_{planing} \cdot v$
   - Quadratic drag: $F_{quad} = C_{planing,quad} \cdot v^2$. This term dominates, so the settled board speed scales with the wind speed instead of running away in strong wind.
   - Planing hydrodynamic lift: raises the board towards the surface as speed increases, clamped by surface contact falloff.

### Course keeping, edging and carving
- **Lateral Resistance**: $F_{lat} = (C_{lat,base} + C_{edge} \cdot |\text{EdgeInput}|) \cdot v_{lat}$, capped at $m / \Delta t$ so one step can at most cancel the sideways speed. The base term is the fins and a neutral stance: with no edge input the board holds its heading against the kite's sideways pull.
- **Edge Drive**: Hydrodynamic lift along the board rail converts lateral holding force into forward thrust: $F_{fwd} = |F_{lat}| \cdot |\text{EdgeInput}| \cdot \eta_{edge}$.
- **Carving** (A / D, left stick X): A carve input turns the board: its heading moves towards $v_{heading} + \text{EdgeInput} \cdot \text{MaxEdgeAngleDeg}$ at up to `CarveTurnRate`. Holding an edge keeps turning, so a steady course is ridden with the edge neutral.
- **Switching stance**: A twin-tip rides either way. When the board is moving tail-first faster than `SwitchStanceSpeedCmS`, nose and tail swap, so after flying the kite to the other side the rider simply rides off on the new tack.
- **Load heel**: The board heels away from the kite by up to `AutoHeelDeg` in proportion to the sideways line force.

### Edge pressure, liftoff and the air
- **Edge pressure** (W / S, left stick Y): scales lateral grip by `EdgePressureGripScale` to the power of the input, so pressing the rail in bites harder (and drags a little more) and flattening the board lets it slide off downwind. It also adds to the pop.
- **Off the plane** the board pivots towards a beam reach on the kite's side at up to `LowSpeedPivotRate`, so a stalled rider is lined up for the kite to pull them back onto the plane.
- **Liftoff**: the board leaves the water when the kite's upward pull exceeds `LiftoffWeightFactor` times the rider's weight. Sending the kite overhead with the bar in does this without a pop.
- **In the air** the carve input spins the board at `AirSpinRate`; left alone it comes back in line with the direction of travel. A twin-tip lands either way round, so only the angle to the board's axis decides between a clean landing and a crash. A skip shorter than 0.25 s and lower than 30 cm is not counted as a jump.

### How the kite drives the board
- The kite is a point flying on the sphere of its lines (`UKiteComponent`). It has a heading; it flies along it at an airspeed of glide ratio times the wind blowing along the lines, and drifts with the wind blowing across them. Nose-out of the window the two cancel, and the kite parks at the window edge.
- **Steering** asks for a direction of travel round the window; the kite stops where the bar is centred. With the **loop** input held, steering turns the kite directly at airspeed / `MinTurnRadiusCm`, so holding the bar over flies a loop.
- **Line tension** follows the kite's airspeed squared: about 450 N parked in 15 kn, close to 3000 N (the `MaxLineTensionN` cap) for a kite looping through the middle of the window.
- The wind the kite feels is the true wind minus the rider's velocity, so the parked position falls back as the board speeds up. That is what limits board speed and upwind angle.
- The bar position is persistent: sheet input moves it at `SheetRatePerSec` and it stays there.
- A ride starts on a beam reach at 12 kn with the kite at clock 65 on that side (`AKiteSurfGameMode::InitializeRide`). In steady 15 kn wind with no input the board settles at about 18 kn; pointed 30 degrees above a beam reach it gains about 2.2 m/s against the wind.

## Default Tunable Properties

All properties are exposed under `UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning")`:

| Property | Default Value | Description |
| :--- | :--- | :--- |
| `MassKg` | `85.0f` | Total mass (rider + board + rig) in kilograms. |
| `BoardLengthCm` | `140.0f` | Length of board in centimeters. |
| `BoardWidthCm` | `42.0f` | Width of board in centimeters. |
| `BuoyancyN` | `1500.0f` | Static buoyancy force in Newtons. |
| `PlaningThresholdCmS` | `400.0f` | Speed threshold for planing transition (~7.78 kn). |
| `DisplacementDragCoef` | `0.1f` | Quadratic displacement drag coefficient. |
| `LinearDisplacementDragCoef` | `8.0f` | Linear displacement drag coefficient. |
| `PlaningDragCoef` | `8.0f` | Linear planing drag coefficient. |
| `PlaningQuadraticDragCoef` | `0.03f` | Quadratic planing drag coefficient. |
| `BaseLateralDragCoef` | `500.0f` | Lateral grip with no edge input (fins, neutral stance). |
| `EdgeGripCoef` | `1700.0f` | Extra lateral grip at full edge input. |
| `EdgeDriveEfficiency` | `0.35f` | Forward drive efficiency gained from rail edging. |
| `MaxEdgeAngleDeg` | `35.0f` | Maximum board roll and yaw carve angle. |
| `MaxBoardSpeed` | `1800.4f` (35 kn) | Velocity magnitude clamp in cm/s. |
| `CarveTurnRate` | `45.0f` | Turn rate at full carve once planing, in degrees per second. |
| `CarveResponse` | `6.0f` | How quickly the carve follows the input (1/s). |
| `EdgePressureGripScale` | `2.5f` | Grip multiplier at full edge pressure; inverse when flattened. |
| `LowSpeedPivotRate` | `120.0f` | Pivot rate towards a beam reach when stopped, in degrees per second. |
| `LiftoffWeightFactor` | `1.5f` | Upward line force, in rider weights, that lifts the board off. |
| `AirSpinRate` | `200.0f` | Board spin rate in the air at full carve, in degrees per second. |
| `SwitchStanceSpeedCmS` | `100.0f` | Tail-first speed at which nose and tail swap. |
| `AutoHeelDeg` | `12.0f` | Heel away from the kite at full sideways load. |
| `BuoyancySpringStiffness` | `3000.0f` | Vertical water surface equilibrium spring constant. |
| `BuoyancyDamping` | `800.0f` | Vertical damping constant. |
| `PlaningLiftCoef` | `50.0f` | Planing hydrodynamic lift force coefficient. |

## Safety Guards & Telemetry
- **NaN / Inf Guard**: `ensureAlwaysMsgf(!Velocity.ContainsNaN(), ...)` in `AKiteRiderPawn::Tick` and `UBoardMovementComponent::TickComponent`.
- **Water Surface Clamp**: Board location Z is constrained within $\pm 20$ cm of water height while planing.
- **HUD Telemetry**: `AKiteSurfHUD` renders normalized $0-360^\circ$ heading alongside knots speed.
