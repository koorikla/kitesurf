# Architecture Specification

## Coordinate & Unit Conventions

Every unit constant and conversion is defined once, in `Source/KiteSurf/Public/KiteSurfUnits.h` (`namespace KiteUnits`): `CmPerM`, `CmPerKnot` (51.44), `UnrealForcePerN` (100), `GravityMS2` (9.81) and `GravityCmS2`, `AirDensityKgM3` (1.225), `WaterDensityKgM3` (1025), and `KnotsToCmS`, `CmSToKnots`, `KnotsToMS`, `MToCm`, `CmToM`, `NToUnrealForce`, `UnrealForceToN`. Code uses these instead of writing the numbers out.

- **Distance / Position**: Centimeters (Unreal units, cm).
- **Velocity**: Centimeters per second (cm/s) at the component APIs.
- **Forces**: kg*cm/s^2 between components (`UKiteComponent::GetLineForce`, `UBoardMovementComponent::AddExternalForce`; 1 N = 100); tensions and debug forces are in N. The kite's aerodynamics and the rider's air drag are done in SI (m, m/s, N) inside the step and converted once at the boundary.
- **Gravity**: `KiteUnits::GravityMS2` for the kite and the board alike, whatever the world settings say.
- **Wind Speed**: knots at the UI, cm/s in the simulation. Default `BaseWind` = 15 kn ≈ 772 cm/s at 10 m.
- **Water Plane**: World Z = 0.
- **Angles**: Degrees (-90° to +90° azimuth relative to wind direction).
- **Tunables** carry their unit in the name (`PlaningDragKgPerS`, `PopImpulseKgCmPerS`, `GravityTurnRadMPerS2`, `RiderDragAreaM2`, ...) or the tooltip.

## Core Contracts & Public APIs

| Class | Method / Property | Description |
|---|---|---|
| `AKiteRiderPawn` | `void StepSimulation(float StepSeconds)` | One fixed step of the whole rig: `Kite->StepKite`, the line force to the board, `BoardMovement->StepBoard`. `Tick` runs as many as the frame holds. |
| `AKiteRiderPawn` | `float GetSimTimeSeconds() const` / `int32 GetLastFrameSimSteps() const` | Time the simulation has advanced (s); how many fixed steps the last frame ran. |
| `AKiteRiderPawn` | `float SimStepSeconds` / `float MaxFrameSeconds` / `int32 MaxSimStepsPerFrame` / `bool bInterpolateRendering` / `bool bStepSimulation` | The fixed step (1/240 s), the frame clamp (0.1 s), the step cap (32), drawing between the last two steps (on), and stepping at all (off for tests that pose the rider by hand). |
| `UKiteComponent` | `void StepKite(float StepSeconds)` | One fixed step of the kite with the rider where they are now; the pawn calls it. |
| `UKiteComponent` | `void UpdateKite(float DeltaTime)` | Advances the kite by any time in steps of at most `MaxStepSeconds`, for tests and a kite nobody else steps. |
| `UKiteComponent` | `float GetSimTimeSeconds() const` | Time the kite's simulation has advanced (s); it samples the wind at this time. |
| `UKiteComponent` | `float GetSteeringDeadTimeSeconds() const` | How long the rider's bar takes to reach the kite at the current sheet: `SteeringDeadTimeSeconds` (0.15 s) plus `DepoweredDeadTimeExtraSeconds` (0.3 s) times `1 - Sheet`. |
| `UKiteComponent` | `float SteeringDragFactor` / `float GravityTurnRadMPerS2` / `float DepoweredTurnRateFactor` / `float ZeroLiftAngleDeg` | Steering drag (attached-flow drag times 1 + 0.6 times the steering), the gravity turn (c_2 over airspeed, 2.4 rad m/s^2 at 12 m^2), the turn rate with the bar out as a fraction of bar in (0.45), the zero-lift angle (0 deg). |
| `UBoardMovementComponent` | `void StepBoard(float StepSeconds)` | One fixed step of the board with the external force added since the last one; the pawn calls it. |
| `UBoardMovementComponent` | `void Simulate(float DeltaTime)` | Advances the board by any time in steps of at most `MaxStepSeconds`, holding a force added before the call for the whole of it; `TickComponent` calls it. |
| `UBoardMovementComponent` | `float GetSimTimeSeconds() const` / `float RiderDragAreaM2` | Time the board's simulation has advanced (s), at which it samples the rider's wind in the air; the drag area of the rider and board in the air (0.7 m^2). |
| `UWindComponent` | `FVector GetWindAtTime(const FVector& WorldLocation, float TimeSeconds) const` | The wind (cm/s) at a place and time: a pure function of position, time, the parameters and `Seed` (profile, travelling gusts, direction drift). The kite and the board sample it at their own simulation time. |
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
| `UKiteComponent` | `float GetAppliedSteer() const` | Steering reaching the kite, -1..1: the bar while looping, the assist's command otherwise; the rider's bar gets there `GetSteeringDeadTimeSeconds()` after it moves. |
| `UBoardMovementComponent` | `bool IsFloating() const` / `float GetFloatDepthCm() const` | Whether the rider is in the water rather than up on the board, and how deep the board sits. |
| `UKiteComponent` | `bool AreLinesTaut() const` | False while the lines are slack: the kite is not flying and the rider feels no pull. |
| `UKiteComponent` | `float GetAngleOfAttackDeg() const` | Airflow angle to the canopy including bar trim; above `StallAngleDeg` the kite is stalled. |
| `UKiteComponent` | `const FKiteStepDebug& GetLastStepDebug() const` | The last fixed step's true and apparent wind at the kite, lift, drag and side force (N), tension, angle of attack, Cl, Cd and taut or slack. |
| `UBoardMovementComponent` | `const FBoardStepDebug& GetLastStepDebug() const` | The last fixed step's grip, drive and drag forces on the board (N) and its leeway (deg), zero in the air; and the air's drag on the rider (N), only in the air. |
| `AKiteRiderPawn` | `int32 GetPhysicsDebugLevel() const` | The level the pawn draws at: the `kite.Physics.Debug` console variable, at least 1 when the kite's `bDrawDebug` is set, 0 in Shipping. |
| Console | `kite.Physics.Debug 0/1/2` | Off; draw the winds and forces at the kite and the rider; also log a `kitecsv` line per fixed step (below). |
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

## Physics debug view: `kite.Physics.Debug`

A console variable registered in `KiteRiderPawn.cpp`, compiled out of Shipping. `UKiteComponent::bDrawDebug` forces level 1 for that kite's pawn.

- `kite.Physics.Debug 1` draws every frame. At the kite: true wind (blue), apparent wind (cyan), lift (green), drag (red), side force (magenta), line tension along the line (yellow, red when slack), heading (white), and airspeed, angle of attack, Cl, Cd, tension, taut or slack, clock and depth, and the fixed steps the frame ran. At the rider: the true wind at `RiderWindHeightCm` (blue), apparent wind (cyan), the line force on the harness (yellow), the board's grip (orange), drive (green) and drag (red) from the last step, the air's drag on the rider in the air (silver, at chest height), and speed, heading, leeway, edge (roll), board state and gust factor. Beside the rider a bar shows the gust factor from 0.5 to 1.5. Arrows are 20 cm per m/s and 0.4 cm per N, so 10 m/s and 500 N are both 2 m long.
- `kite.Physics.Debug 2` also logs one line per fixed step to `LogKiteSurf`, starting with a header: `kitecsv,t_s,rider_x,rider_y,rider_z,rider_vx,rider_vy,rider_vz,kite_x,kite_y,kite_z,kite_vx,kite_vy,kite_vz,tension_n,alpha_deg,cl,board_state,gust` (cm, cm/s, N, deg; `board_state` is `EBoardState` as a number). `grep -o 'kitecsv,.*' Saved/Logs/KiteSurf.log | cut -d, -f2-` turns a ride into a CSV.

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

The pawn owns the simulation (`AKiteRiderPawn::Tick`, `KiteRiderPawn.cpp`). The kite and board components do not tick on their own under it (`bCanEverTick` is false); the wind has no tick and is sampled where it is needed.

1. **Inputs**: the held sheet input moves the bar; the mouse bar is read.
2. **Restore**: if the root is still where the last frame drew it, it goes back to the simulation's own transform; if something outside the step loop moved it (a reset, a test), that is the new truth.
3. **Fixed steps**: the frame time, clamped to `MaxFrameSeconds`, is added to an accumulator, and `StepSimulation(SimStepSeconds)` runs while the accumulator holds a step, at most `MaxSimStepsPerFrame` times (a hitch drops the rest). Each step, in this order:
   1. **Kite** (`UKiteComponent::StepKite`): wind at the kite at the kite's simulation time; the rider's bar as it was one dead time ago; the assist; lift, drag, side force and weight; the line constraint and tension; the line force on the rider.
   2. **Line force** handed to the board (`AddExternalForce`).
   3. **Board** (`UBoardMovementComponent::StepBoard`): water sample, buoyancy, lift-off, drag and grip as exact decays, edge drive, the rider's air drag in the air, orientation, move, landing.
   4. With `kite.Physics.Debug 2`, a `kitecsv` line.
4. **Draw between steps**: the root and the kite's mesh are placed between the last two simulation states by the accumulator's remainder (`bInterpolateRendering`).
5. **Rider, camera, audio and debug**: the rider stands square across the board and turns with it (feet in the straps), leaning against the kite's pull, and tells the kite where the bar is; the camera looks along the heading, turned towards the kite far enough to keep it in frame; the sound loops follow the apparent wind, board speed and line tension; `kite.Physics.Debug 1` draws.
6. **HUD and wake** read the drawn transforms: the HUD's wind indicator, wind window and speedometer, and `UBoardWakeComponent` (post-physics) lays foam and throws spray from the board's new position.
