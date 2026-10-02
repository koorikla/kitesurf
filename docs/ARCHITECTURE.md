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
| `UWindComponent` | `FVector GetWindAtTime(const FVector& WorldLocation, float TimeSeconds) const` | The wind (cm/s) at a place and time: a pure function of position, time, the parameters and `Seed` (profile, travelling gusts, direction drift). The kite samples it at its own simulation time. |
| `UWindComponent` | `FVector GetWindAt(const FVector& WorldLocation) const` | The same at the current time (world time, or `TimeOverride` without a world). |
| `UWindComponent` | `FVector BaseWind` | Mean wind at `ReferenceHeightCm` (default: `(772, 0, 0)` cm/s, 15 kn along +X); its direction is the mean direction. Set from the gear screen's wind by `AKiteSurfGameMode::InitializeRide` only. |
| `UWindComponent` | `float ReferenceHeightCm` / `float ShearExponent` / `float MinSampleHeightCm` | Power-law profile: wind at z is `(max(z, MinSampleHeightCm) / ReferenceHeightCm) ^ ShearExponent` of `BaseWind` (defaults 1000 cm, 0.11, 100 cm: 0.78 at 1 m, 0.81 at 1.5 m, 1.11 at 25 m). |
| `UWindComponent` | `float GustStrength` | The strongest gusts reach `1 + GustStrength` times the mean and the deepest lulls `1 - GustStrength`, and never go past (default 0.3). The gust noise is normalised so its 99th percentile is `1 + GustStrength`. |
| `UWindComponent` | `float GustCellLengthCm` / `GustPuffRate` / `GustPuffShare` / `GustEvolveSeconds` | Gusts are two octaves of seeded gradient noise carried downwind at the mean speed: cells 60 m along the wind and half that across, puffs 3.5 times finer with 40% of the variation, the pattern changing over 180 s (correlation 0.5 after about 80 s), so a gust seen upwind arrives `distance / U` later. |
| `UWindComponent` | `float DirectionDriftDeg` | Standard deviation of the wind direction about the mean (default 5 deg); never more than twice that. |
| `UWindComponent` | `int32 Seed` | The same seed and parameters give the same wind everywhere at every time; another seed gives other gusts. |
| `UKiteComponent` | `float RiderWindHeightCm` | Where the rider feels the wind (default 150 cm above the feet): the window axis, the downwind direction and the wind in the rider's ears are sampled there. The HUD quotes the wind at `ReferenceHeightCm`. |
| `UKiteWindMath` | Static Math Library | `KnotsToCmPerSec`, `ApparentWind`, `WindWindowAzimuthDeg`, `KitePositionInWindow`. |
| `AKiteRiderPawn` | `void SteerKite(float Axis)` | Bar steering. Towards the other side of the window: the kite is flown there over the top (right = clockwise looking downwind). Towards the kite's own side: the bar turns it directly, which loops it. Centred: the kite drifts up the window edge to the zenith and sits there (`UKiteComponent::ZenithDriftGain`, `ZenithDriftMaxHeadingDeg`), or with `UKiteComponent::bParkHoldAssist` stays at the clock position it had when the bar was centred. |
| `AKiteRiderPawn` | `static FRideAudioMix ComputeAudioMix(float ApparentWindKnots, float BoardSpeedKnots, bool bOnWater, float LineTensionN)` | Volume and pitch for the wind, water and line loops. The pawn eases its three looping audio components towards it every tick. |
| `UKiteComponent` | `bool IsLooping() const` | True while the bar is going straight to the kite and turning it round. |
| `UKiteComponent` | `void SetLoopHeld(bool bHeld)` | Forces raw steering from any position. Not bound to a key; for scripted input and tests. |
| `UKiteComponent` | `float GetTurnDeg() const` | Degrees turned under the current steering input; 360 is one loop. |
| `UBoardMovementComponent` | `void SetWeightShift(float Value)` | Rider weight along the board in `[-1.0, 1.0]`: +1 on the nose, -1 on the tail. |
| `UKiteComponent` | `void SetKiteSize(float AreaM2)` / `static float RecommendKiteSizeM2(float WindKnots, float RiderMassKg)` | Rigs a kite of that area (mass and turning radius follow); the size a rider would pick for the wind. |
| `AKiteSurfSpot` | `void Setup(AKiteRiderPawn*, const FVector& Origin, const FVector& DownwindDir, bool bIslands, bool bSandbars, bool bSharks)` / `float GetSandHeightCm(const FVector&) const` | Lays out the sandbars, islands and sharks round the start and watches the rider; the height of sand above the water at a point. |
| `UKiteComponent` | `void SetKiteModel(EKiteModel)` | Rigs the loop (3 strut) or boost (5 strut) kite at the current size. |
| `UBoardMovementComponent` | `void SetBoardSize(EBoardSize)` | Rides the 132, 138 or 145: pop, planing speed, drag, grip and turn rate follow it. |
| `UKiteComponent` | `void SetBarEnds(const FVector&, const FVector&)` | The rider says where the bar is; the lines run from there. |
| `AKiteRiderPawn` | `FVector GetHarnessHookWorldPosition() const` | Where the lines pull on the rider: the front of the waist. |
| `UKiteComponent` | `float GetAppliedSteer() const` | Steering reaching the kite, -1..1: the bar while looping, the assist's command otherwise. |
| `UBoardMovementComponent` | `bool IsFloating() const` / `float GetFloatDepthCm() const` | Whether the rider is in the water rather than up on the board, and how deep the board sits. |
| `UKiteComponent` | `bool AreLinesTaut() const` | False while the lines are slack: the kite is not flying and the rider feels no pull. |
| `UKiteComponent` | `float GetAngleOfAttackDeg() const` | Airflow angle to the canopy including bar trim; above `StallAngleDeg` the kite is stalled. |
| `UWindComponent` | `float GetGustFactorAt(const FVector&) const` / `GetGustFactorAtTime(const FVector&, float)` | Wind over base wind at the reference height above that place, now or at a given time: above 1 in a gust, below 1 in a lull. |
| `UKiteComponent` | `bool IsCrashed() const` | True while the kite lies on the water; `OnKiteCrashed` / `OnKiteRelaunched` fire on the way in and out. |
| `AKiteRiderPawn` | `void SetRiderCharacter(ERiderCharacter)` | Shows Santa, the wetsuit rider or the robot; the choice is stored by `UKiteSurfGameInstance`. |
| `AKiteRiderPawn` | `void SheetKite(float Amount)` | Sets the bar position, which persists; `Amount` clamped to `[0.0, 1.0]`. |
| `AKiteRiderPawn` | `void SetSheetRateInput(float Axis)` | Held sheet-in/out input; moves the bar at `SheetRatePerSec`. |
| `UKiteComponent` | `void SetWindowPosition(float ClockDeg, float DepthDeg)` | Places the kite by clock position and depth in the wind window. |
| `UKiteComponent` | `FVector GetWindowAxis() const` | Axis of the wind window: where the apparent wind blows towards. |
| `UBoardWakeComponent` | `void EmitSplash(float Intensity)` | Throws a ring of spray; foam trail and rail spray are automatic. |
| `AKiteSurfGameMode` | `static void InitializeRide(AKiteRiderPawn*, float SpeedCmS)` | Starts the pawn on a beam reach with the kite powered up. |
| `AKiteRiderPawn` | `FVector GetBoardVelocity() const` | Returns current board velocity vector. |
| `AKiteRiderPawn` | `float GetKiteAzimuthDeg() const` | Returns kite azimuth angle in degrees (`[-90.0, 90.0]`). |

## Level Contract: `/Game/Maps/L_OpenWater`

- Must contain a `WaterBodyOcean` or water plane positioned at world Z = 0.
- Must contain a `PlayerStart` positioned above or on the water plane.
- Default GameMode is `/Script/KiteSurf.KiteSurfGameMode`.

## Game Instance, Settings & Master Volume

- **GameInstance**: `UKiteSurfGameInstance` persists user settings across level transitions (`L_MainMenu` -> `L_OpenWater`).
- **Settings Persistence**: Game-specific gameplay settings (wind strength: 8–40 kn, kite size: recommended or 5–17 m², kite model, board size, rider, spot features, master volume: 0.0–1.0) save and load via `UKiteSurfSaveGame`. Display and engine scalability settings (resolution, window mode, VSync, and overall quality scalability presets Low/Medium/High/Epic) are managed by `UGameUserSettings` (`Saved/Config/Linux/GameUserSettings.ini`) and mapped via `Config/DefaultScalability.ini`.
- **Rider Character Materials**: Mannequin materials and textures (`MI_Manny_01_New`, `MI_Manny_02_New`, `M_Mannequin`, and physics asset `PA_Mannequin`) are committed directly under `Content/Characters/Mannequins/` from the Unreal Engine mannequin template assets to ensure `SKM_Manny_Simple` dependencies resolve cleanly without `LoadErrors`.
- **Master Volume**: Controlled via `FApp::SetVolumeMultiplier(MasterVolume)` both when settings are loaded from disk (`LoadSettingsFromDisk`) and when updated interactively (`SetMasterVolume`).
- **Entry Level & Maps**: In standalone/packaged runs (`-game`), `GameDefaultMap` is `/Game/Maps/L_MainMenu` and uses `AKiteSurfMainMenuGameMode` configured via `L_MainMenu.umap` WorldSettings. `EditorStartupMap` remains `/Game/Maps/L_OpenWater`.

## Tick Execution Order

Physics simulation and component updates execute in the following sequential order:
1. **Wind Simulation**: `UWindComponent` evaluates ambient wind vector and spatial gusts.
2. **Kite Aerodynamics**: Kite azimuth, apparent wind, lift, and steering pull calculated.
3. **Board Hydrodynamics**: Board hull planning forces, fin resistance, drag, and velocity updates. Z position clamped to water surface (Z=0).
4. **Rider, camera & HUD**: rider stands square across the board and turns with it (feet in the straps), leaning against the kite's pull; the camera looks along the heading, turned towards the kite far enough to keep it in frame; HUD wind indicator, wind window and speedometer update.
5. **Wake**: `UBoardWakeComponent` (post-physics) lays foam and throws spray from the board's new position.
