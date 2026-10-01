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
   - Quadratic high-speed spray drag: $F_{spray} = 0.015 \cdot v^2$
   - Planing hydrodynamic lift: raises the board towards the surface as speed increases, clamped by surface contact falloff.

### Edging & Lateral Resistance
- **Lateral Resistance**: $F_{lat} = (C_{lat,base} + C_{edge} \cdot |\text{EdgeInput}|) \cdot v_{lat}$.
- **Edge Drive**: Hydrodynamic lift along the board rail converts lateral holding force into forward thrust: $F_{fwd} = |F_{lat}| \cdot |\text{EdgeInput}| \cdot \eta_{edge}$.
- **Heading & Carving**: Edging carves board heading towards the desired angle ($v_{heading} + \text{EdgeInput} \cdot \text{MaxEdgeAngleDeg}$) at rate `CarveTurnRate`.

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
| `PlaningDragCoef` | `45.0f` | Linear planing drag coefficient. |
| `EdgeGripCoef` | `400.0f` | Lateral grip resistance multiplier when edging. |
| `EdgeDriveEfficiency` | `0.35f` | Forward drive efficiency gained from rail edging. |
| `MaxEdgeAngleDeg` | `35.0f` | Maximum board roll and yaw carve angle. |
| `MaxBoardSpeed` | `1800.4f` (35 kn) | Velocity magnitude clamp in cm/s. |
| `CarveTurnRate` | `90.0f` | Angular turning rate in degrees per second. |
| `BuoyancySpringStiffness` | `3000.0f` | Vertical water surface equilibrium spring constant. |
| `BuoyancyDamping` | `800.0f` | Vertical damping constant. |
| `PlaningLiftCoef` | `50.0f` | Planing hydrodynamic lift force coefficient. |

## Safety Guards & Telemetry
- **NaN / Inf Guard**: `ensureAlwaysMsgf(!Velocity.ContainsNaN(), ...)` in `AKiteRiderPawn::Tick` and `UBoardMovementComponent::TickComponent`.
- **Water Surface Clamp**: Board location Z is constrained within $\pm 20$ cm of water height while planing.
- **HUD Telemetry**: `AKiteSurfHUD` renders normalized $0-360^\circ$ heading alongside knots speed.
