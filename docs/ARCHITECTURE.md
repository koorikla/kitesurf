# Architecture Specification

## Coordinate & Unit Conventions

- **Distance / Position**: Centimeters (Unreal units, cm)
- **Velocity**: Centimeters per second (cm/s)
- **Wind Speed**: 1 knot = 51.44 cm/s. Default BaseWind = 15 knots ≈ 772 cm/s.
- **Water Plane**: World Z = 0.
- **Angles**: Degrees (-90° to +90° azimuth relative to wind direction).

## Core Contracts & Public APIs

| Class | Method / Property | Description |
|---|---|---|
| `UWindComponent` | `FVector GetWindAt(const FVector& WorldLocation) const` | Returns the wind vector at a given 3D position. |
| `UWindComponent` | `FVector BaseWind` | Default baseline wind vector (default: `(772, 0, 0)` cm/s). |
| `AKiteRiderPawn` | `void SteerKite(float Axis)` | Steers kite left/right; `Axis` clamped to `[-1.0, 1.0]`. |
| `AKiteRiderPawn` | `void SheetKite(float Amount)` | Pulls/releases kite bar; `Amount` clamped to `[0.0, 1.0]`. |
| `AKiteRiderPawn` | `FVector GetBoardVelocity() const` | Returns current board velocity vector. |
| `AKiteRiderPawn` | `float GetKiteAzimuthDeg() const` | Returns kite azimuth angle in degrees (`[-90.0, 90.0]`). |

## Level Contract: `/Game/Maps/L_OpenWater`

- Must contain a `WaterBodyOcean` or water plane positioned at world Z = 0.
- Must contain a `PlayerStart` positioned above or on the water plane.
- Default GameMode is `/Script/KiteSurf.KiteSurfGameMode`.

## Tick Execution Order

Physics simulation and component updates execute in the following sequential order:
1. **Wind Simulation**: `UWindComponent` evaluates ambient wind vector and spatial gusts.
2. **Kite Aerodynamics**: Kite azimuth, apparent wind, lift, and steering pull calculated.
3. **Board Hydrodynamics**: Board hull planning forces, fin resistance, drag, and velocity updates. Z position clamped to water surface (Z=0).
4. **Camera & HUD**: SpringArm lag, camera position, HUD wind indicator, and speedometer update.
