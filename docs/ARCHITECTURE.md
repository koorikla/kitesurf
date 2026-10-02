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
| `UWindComponent` | `FVector GetWindAt(const FVector& WorldLocation) const` | Returns the wind vector at a given 3D position (including gusts, drift, shear). |
| `UWindComponent` | `FVector BaseWind` | Default baseline wind vector (default: `(772, 0, 0)` cm/s). |
| `UWindComponent` | `float GustStrength` | Fraction of base speed variation for gusts (0..1, default: 0.3). |
| `UWindComponent` | `float GustPeriodSeconds` | Gust fluctuation period in seconds (default: 8.0s). |
| `UWindComponent` | `float DirectionDriftDeg` | Maximum wind direction drift in degrees (default: 10.0°). |
| `UWindComponent` | `float ShearHeightCm` | Height at which wind reaches full speed (default: 1000 cm; 70% at Z=0). |
| `UKiteWindMath` | Static Math Library | `KnotsToCmPerSec`, `ApparentWind`, `WindWindowAzimuthDeg`, `KitePositionInWindow`. |
| `AKiteRiderPawn` | `void SteerKite(float Axis)` | Bar steering. Towards the other side of the window: the kite is flown there over the top (right = clockwise looking downwind). Towards the kite's own side: the bar turns it directly, which loops it. |
| `AKiteRiderPawn` | `static FRideAudioMix ComputeAudioMix(float ApparentWindKnots, float BoardSpeedKnots, bool bOnWater, float LineTensionN)` | Volume and pitch for the wind, water and line loops. The pawn eases its three looping audio components towards it every tick. |
| `UKiteComponent` | `bool IsLooping() const` | True while the bar is going straight to the kite and turning it round. |
| `UKiteComponent` | `void SetLoopHeld(bool bHeld)` | Forces raw steering from any position. Not bound to a key; for scripted input and tests. |
| `UKiteComponent` | `float GetTurnDeg() const` | Degrees turned under the current steering input; 360 is one loop. |
| `UBoardMovementComponent` | `void SetWeightShift(float Value)` | Rider weight along the board in `[-1.0, 1.0]`: +1 on the nose, -1 on the tail. |
| `UKiteComponent` | `void SetKiteSize(float AreaM2)` / `static float RecommendKiteSizeM2(float WindKnots, float RiderMassKg)` | Rigs a kite of that area (mass and turning radius follow); the size a rider would pick for the wind. |
| `UKiteComponent` | `void SetKiteModel(EKiteModel)` | Rigs the loop (3 strut) or boost (5 strut) kite at the current size. |
| `UBoardMovementComponent` | `void SetBoardSize(EBoardSize)` | Rides the 132, 138 or 145: pop, planing speed, drag, grip and turn rate follow it. |
| `UKiteComponent` | `void SetBarEnds(const FVector&, const FVector&)` | The rider says where the bar is; the lines run from there. |
| `AKiteRiderPawn` | `FVector GetHarnessHookWorldPosition() const` | Where the lines pull on the rider: the front of the waist. |
| `UKiteComponent` | `float GetAppliedSteer() const` | Steering reaching the kite, -1..1: the bar while looping, the assist's command otherwise. |
| `UBoardMovementComponent` | `bool IsFloating() const` / `float GetFloatDepthCm() const` | Whether the rider is in the water rather than up on the board, and how deep the board sits. |
| `UKiteComponent` | `bool AreLinesTaut() const` | False while the lines are slack: the kite is not flying and the rider feels no pull. |
| `UKiteComponent` | `float GetAngleOfAttackDeg() const` | Airflow angle to the canopy including bar trim; above `StallAngleDeg` the kite is stalled. |
| `UWindComponent` | `float GetGustFactorAt(const FVector&) const` | Current wind over base wind: above 1 in a gust, below 1 in a lull. |
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
- **Settings Persistence**: Game-specific gameplay settings (wind strength: 8–40 kn, kite size: recommended or 5–17 m², kite model, board size, rider, master volume: 0.0–1.0) save and load via `UKiteSurfSaveGame`. Display and engine scalability settings (resolution, window mode, VSync, and overall quality scalability presets Low/Medium/High/Epic) are managed by `UGameUserSettings` (`Saved/Config/Linux/GameUserSettings.ini`) and mapped via `Config/DefaultScalability.ini`.
- **Rider Character Materials**: Mannequin materials and textures (`MI_Manny_01_New`, `MI_Manny_02_New`, `M_Mannequin`, and physics asset `PA_Mannequin`) are committed directly under `Content/Characters/Mannequins/` from the Unreal Engine mannequin template assets to ensure `SKM_Manny_Simple` dependencies resolve cleanly without `LoadErrors`.
- **Master Volume**: Controlled via `FApp::SetVolumeMultiplier(MasterVolume)` both when settings are loaded from disk (`LoadSettingsFromDisk`) and when updated interactively (`SetMasterVolume`).
- **Menu videos**: `UKiteSurfMenuVideoSubsystem` (a game instance subsystem) plays `Content/Movies/Intro.webm` once when the main menu first opens (any key or click skips it; it ends on white, which fades off the menu) and then `Content/Movies/MenuLoop.webm` behind the main menu and the gear screen, through `KiteSurfMenuStyle::BuildBackdrop`. Playback is the Electra player with its VP9 decoder; `Config/DefaultGame.ini` stages `Content/Movies` as loose files. Without a renderer, without the files, or if the intro shows no frame within 4 s, the menus show the still `T_MenuBackground`. `PrepareLoadingScreen()` stops the loop and sets the movie player's loading screen before the ride level opens. The videos are filmed in the game by `scripts/render-menu-video.sh` with `AKiteSurfCinematicCamera` (`kitesurf.Shot`) and `kitesurf.CaptureFrames`.
- **Entry Level & Maps**: In standalone/packaged runs (`-game`), `GameDefaultMap` is `/Game/Maps/L_MainMenu` and uses `AKiteSurfMainMenuGameMode` configured via `L_MainMenu.umap` WorldSettings. `EditorStartupMap` remains `/Game/Maps/L_OpenWater`.

## Tick Execution Order

Physics simulation and component updates execute in the following sequential order:
1. **Wind Simulation**: `UWindComponent` evaluates ambient wind vector and spatial gusts.
2. **Kite Aerodynamics**: Kite azimuth, apparent wind, lift, and steering pull calculated.
3. **Board Hydrodynamics**: Board hull planning forces, fin resistance, drag, and velocity updates. Z position clamped to water surface (Z=0).
4. **Rider, camera & HUD**: rider stands square across the board and turns with it (feet in the straps), leaning against the kite's pull; the camera looks along the heading, turned towards the kite far enough to keep it in frame; HUD wind indicator, wind window and speedometer update.
5. **Wake**: `UBoardWakeComponent` (post-physics) lays foam and throws spray from the board's new position.
