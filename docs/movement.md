# Movement Mechanics & Hydrodynamics

This document outlines the board movement, edging, planing transition, and physics tuning constants implemented in `UBoardMovementComponent` and `AKiteRiderPawn`.

## Physics Architecture

### Fixed step
The pawn steps the whole rig itself, at `AKiteRiderPawn::SimStepSeconds` (1/240 s) whatever the frame rate, in one order: the kite with the rider where they are (`UKiteComponent::StepKite`), the line force handed to the board, then the board (`UBoardMovementComponent::StepBoard`). `AKiteRiderPawn::Tick` runs as many of those steps as the frame holds (`StepSimulation`), clamps a long frame to `MaxFrameSeconds` (0.1 s) and runs at most `MaxSimStepsPerFrame` (32) steps, so a hitch slows the ride for a moment instead of throwing it. The rider and the kite are drawn between the last two steps (`bInterpolateRendering`). The kite and the board do not tick on their own under a pawn; `UKiteComponent::UpdateKite` and `UBoardMovementComponent::Simulate` / `TickComponent` still advance one by any time in steps of at most `MaxStepSeconds`, for tests and for components nobody else steps. The same ride at 30, 60 and 120 frames a second comes out identical (`KiteSurf.Physics.StepRateIndependent`). Units, constants and conversions are in `KiteSurfUnits.h`; the forces are worked out in SI inside each step.

### Dual Drag Regimes
1. **Displacement Regime** ($v < 400 \text{ cm/s}$):
   - Quadratic drag: $F_{quad} = C_{disp} \cdot v^2$
   - Linear drag: $F_{lin} = C_{disp,lin} \cdot v$
   - Rapid deceleration when unpowered: bringing rider from displacement speeds to $< 2$ knots within 5 seconds.
2. **Planing Regime** ($v \ge 400 \text{ cm/s}$):
   - Linear drag: $F_{lin} = C_{planing} \cdot v$
   - Quadratic drag: $F_{quad} = C_{planing,quad} \cdot v^2$. This term dominates, so the settled board speed scales with the wind speed instead of running away in strong wind.
   - Planing hydrodynamic lift: raises the board towards the surface as speed increases, clamped by surface contact falloff.

### Floating and the water start
The board only carries the rider at speed. Below `FloatUntilSpeedFraction` of the planing threshold the rider floats with the board `FloatSubmersionCm` under the surface (about chest deep) and lies back in the water; between there and the planing threshold they rise, and at planing speed the board rides on the surface. The depth follows the speed at `FloatResponse`, so a rider who loses the kite sinks over a second or so and comes back up as the kite gets them going again. A rider in the air lands on the surface first and sinks from there. `IsFloating()` and `GetFloatDepthCm()` report it; the HUD state line reads Floating, Getting up or Planing.

### Course keeping, edging and carving
- **Lateral Resistance**: $F_{lat} = (C_{lat,base} + C_{edge} \cdot |\text{EdgeInput}|) \cdot v_{lat}$ (`BaseGripKgPerS`, `EdgeGripKgPerS`), integrated exactly in each step ($v_{lat} \leftarrow v_{lat} e^{-c \Delta t / m}$), so it never overshoots at any step length. The forward drag is the closed-form decay of its linear plus quadratic terms in the same way. The base term is the fins and a neutral stance: with no edge input the board holds its heading against the kite's sideways pull.
- **Edge Drive**: Hydrodynamic lift along the board rail converts lateral holding force into forward thrust: $F_{fwd} = |F_{lat}| \cdot |\text{EdgeInput}| \cdot \eta_{edge}$.
- **Turning** (A / D, left stick X): A turn input carves the board: its heading moves towards $v_{heading} + \text{EdgeInput} \cdot \text{MaxEdgeAngleDeg}$ at up to `CarveTurnRate`. Holding an edge keeps turning, so a steady course is ridden with the edge neutral.
- **Switching stance**: A twin-tip rides either way. When the board is moving tail-first faster than `SwitchStanceSpeedCmS`, nose and tail swap, so after flying the kite to the other side the rider simply rides off on the new tack.
- **Load heel**: The board heels away from the kite by up to `AutoHeelDeg` in proportion to the sideways line force.

### Weight shift, liftoff and the air
- **Weight shift** (W / S, left stick Y): +1 is weight on the nose, -1 on the tail. Weight back sinks the tail: lateral grip is scaled by `TailWeightGripScale`, drag rises by `TailWeightDrag`, the board heels further, the nose lifts, and the pop gains `TailWeightPopBonus`. Weight forward flattens the board: grip drops by the same factor, planing drag falls by `NoseWeightDragSaving`, and the nose dips. In the air it tips the board by up to `AirWeightShiftPitchDeg`.
- **Off the plane** the board pivots towards a beam reach on the kite's side at up to `LowSpeedPivotRate`, so a stalled rider is lined up for the kite to pull them back onto the plane.
- **Load** (the jump button held): the rider crouches with their weight over the back of the board. The load builds inside the step at `LoadRatePerSec` and lets go at `LoadReleaseRatePerSec`; it multiplies the sideways grip by up to `1 + LoadGripBonus` and holds the board down like a full edge. Letting go pops; see `docs/jumping.md`.
- **Liftoff**: the board leaves the water when the kite's upward pull exceeds `LiftoffWeightFactor` times the rider's weight, or up to `EdgedLiftoffWeightBonus` more when the rider is edging or loading. Holding the edge (or the load) while the kite is sent, then popping, is how a jump is loaded; see `docs/jumping.md`.
- **In the air** the carve input spins the board at `AirSpinRate`; left alone it comes back in line with the direction of travel. The air drags on the rider and board, $0.5 \rho C_D A |v_a| v_a$ with `RiderDragAreaM2` (0.7 m^2) and $v_a$ the wind at chest height minus their velocity: 97 N at 15 m/s in still air (`KiteSurf.Jump.BodyDragInTheAir`). Nothing like it acts on the water, where the hull's drag and grip are the model. A twin-tip lands either way round, so only the angle to the board's axis decides between a clean landing and a crash. A skip shorter than 0.25 s and lower than 30 cm is not counted as a jump.

### How the kite drives the board
The kite (`UKiteComponent`) is a point mass on the end of its lines, stepped by the pawn at `SimStepSeconds` (1/240 s) with the forces in SI units.

- **Forces on it**: lift and drag from the air flowing over it (true wind minus its own velocity), a side force that resists sliding sideways, gravity, and the pull of the lines.
- **Angle of attack** is the angle of that airflow to the canopy plus the trim from the bar (`TrimSheetedOutDeg` to `TrimSheetedInDeg`). Lift rises from zero at `ZeroLiftAngleDeg` (0 by default; a cambered canopy is about -3) to `MaxLiftCoefficient` at `StallAngleDeg`; past that the kite is stalled and makes mostly drag. Bending the canopy into a turn costs drag: attached-flow drag is multiplied by `1 + SteeringDragFactor * |steer|` (0.6). Air on the wrong side of the canopy (overflying the window, or a fully depowered kite flown fast) luffs it.
- **The lines only pull.** They hold the kite at line length while the forces on it point away from the rider; the tension is whatever that takes, so it rises with the square of the kite's airspeed and falls to nothing in a lull. When it reaches zero the lines are slack: the kite stops flying and falls like a sheet (`SlackDragCoefficient`) until the lines snatch tight again or it reaches the water.
- **Parking** is not scripted: with its nose out of the window the kite settles where lift, drag and line tension balance, about 10 degrees inside the window edge, and further back the faster the rider goes. That is what limits board speed and upwind angle.
- **Steering** turns the nose at airspeed / `MinTurnRadiusCm`, wound in at `TurnResponse`, so the tightest turn has the same radius at any speed: about 6.3 m measured from the kite's path at 15 kn and 6.5 m at 25 kn, 2.3 s and 1.4 s a loop (`KiteSurf.Kite.LoopRadiusIsSpeedIndependent`). Sheeted out the bar turns the kite at `DepoweredTurnRateFactor` (0.45) of the rate sheeted in, following the sheet in between, and the rider's bar reaches the kite after a dead time of `SteeringDeadTimeSeconds` (0.15 s) plus `DepoweredDeadTimeExtraSeconds` (0.3 s) times how far out the bar is (`GetSteeringDeadTimeSeconds`): depowered, the kite turns 36% as far in the same time (`KiteSurf.Kite.DepowerSlowsTheTurn`). Gravity turns the nose of a slow kite flying across the window down, at `GravityTurnRadMPerS2` (2.4 rad m/s^2 for 12 m^2, more for smaller kites) over the airspeed. The assist below acts on the bar as it reaches the kite, and its own corrections are not delayed. There is no loop key. Bar towards the other side of the window is a request to fly there: an assist takes the kite over the top, and turns the nose up before it reaches the water. Bar centred lets the kite do what a real one does with the bar neutral: it drifts up the window edge to the zenith and sits there, the assist leaning its nose towards 12 by `ZenithDriftGain` degrees per degree of clock still to go, up to `ZenithDriftMaxHeadingDeg` (from clock 65, about 2 o'clock, to within 10 degrees of 12 in about 12 s in 15 kn, standing). With `bParkHoldAssist` on, bar centred instead holds the clock position the kite had when the bar was centred (the assist leans the nose against gravity and gusts, `ParkHoldGain`, `ParkHoldMaxDeg`). Bar towards the side the kite is already on, once it is more than `LoopClockDeg` round that side, goes straight to the kite: it turns down and round in a loop for as long as the bar is held, and a loop taken too low goes into the water. Reversing or centring the bar ends the loop.
- **In the water** the lines are slack; the kite relaunches after `RelaunchDelaySeconds`, or sooner if steered, provided there is wind to fly in.
- The pull passed to the rider is capped at `MaxLineTensionN` (6000 N), which stands in for line stretch.
- **Size**: `SetKiteSize` rigs a kite of a given area and scales its mass, the air it has to push and its turning radius with it (12 m^2: 3 kg, 4.2 m radius). `RecommendKiteSizeM2` gives the size a rider would rig for the wind, about 2.2 x rider kg / knots, from the sizes on offer (2 to 17 m^2; the 2, 3 and 4 m^2 kites are for 50 kn and up). The wind can be set from 8 to 90 kn (`KiteGear::MinWindKnots`, `MaxWindKnots`). The game rigs that size unless one is chosen in Settings; the HUD shows it next to the wind.
- The bar position is persistent: sheet input moves it at `SheetRatePerSec` (2.5 per second: the whole throw in 0.4 s) and it stays there. It sets the kite's trim between `TrimSheetedOutDeg` (-22) and `TrimSheetedInDeg` (+2). On the 9 m in 20 kn that is 170 N and 9 kn with the bar out, 370 N and 14 kn half way, 1120 N and 22 kn with it in (`KiteSurf.Ride.BarIsTheThrottle`).
- A ride starts on a beam reach at 12 kn with the kite at clock 65 on that side (`AKiteSurfGameMode::InitializeRide`). Hands off, the kite climbs towards 12 and the rider is still planing after 5 s but off the plane by 10 s (`KiteSurf.Ride.KeepsPlaningWithoutInput`). With the kite held at clock 65 (the park-hold assist) in steady 15 kn wind a 12 m^2 kite and an 81 kg rider settle at about 15 kn with about 510 N in the lines (0.64 body weights), and in 20 kn at 19.5 kn with 840 N (`KiteSurf.Physics.SteadyRideAcross`). Held by the fins 25 degrees above a beam reach the board makes 1.7 m/s against the wind with 9.6 degrees of leeway (`KiteSurf.Physics.UpwindAtEdgeAngle`); 30 degrees above, in the default gusts, about 2 m/s (`KiteSurf.Movement.UpwindAngle`). A gust from 15 to 22 kn nearly doubles the pull and adds 6 kn within 5 s; a lull to 3 kn drops the kite at once (`KiteSurf.Physics.GustHitsMidRide`, `LullDropsKite`).

### Wind
`UWindComponent` is a pure function of position, time and `Seed` (`GetWindAtTime`), so the kite, the rider, the HUD and the sound all sample the same wind and a ride can be replayed. The kite samples it at its own simulation time, and so does the board for the rider's drag in the air, so the ride does not depend on the frame rate. The base wind is the wind at `ReferenceHeightCm` (10 m, as forecasts quote it); with height it follows a power law with `ShearExponent` 0.11, sampled no lower than `MinSampleHeightCm`, so the rider at chest height (`UKiteComponent::RiderWindHeightCm`, 1.5 m) feels about 0.81 of it and the kite at 25 m about 1.11. Gusts are two octaves of seeded noise over the water (`GustCellLengthCm` 60 m along the wind and half that across, puffs `GustPuffRate` times finer with `GustPuffShare` of the variation) carried downwind at the mean wind speed and changing over `GustEvolveSeconds`, so a gust seen upwind arrives a little later, and a cell takes about 8 s to pass in 15 kn. They reach `1 + GustStrength` and `1 - GustStrength` and never go past. The direction wanders by `DirectionDriftDeg` (standard deviation, 5 degrees) and never more than twice that. `GetGustFactorAt` gives the current wind over the base wind at the reference height; the HUD calls out GUST and LULL from it.

The wind's direction is shown two ways. `UWindStreakComponent` keeps a field of long thin foam streaks on the water round the rider, lying along the wind and drifting down it at `DriftFraction` of its speed; they fade in from `MinWindKnots` and are not there in a calm. They read the wind at the water, which the profile gives at `MinSampleHeightCm` (about 0.78 of the base wind). The HUD's WIND dial (`AKiteSurfHUD::DrawWindFlag`) is a flag seen from above with the top of the dial the way the camera looks (`GetWindOnScreen`), and says in words where the wind comes from; it quotes the wind at `ReferenceHeightCm`, as the telemetry does.

## Default Tunable Properties

`UBoardMovementComponent`, all `UPROPERTY(EditAnywhere, BlueprintReadWrite)` under `Tuning` (the jump ones are in `docs/jumping.md`). Forces in the component are kg*cm/s^2 (1 N = 100); the units are in the names.

| Property | Default | Description |
| :--- | :--- | :--- |
| `MassKg` | `85` | Rider, board and rig (kg). |
| `BoardLengthCm` / `BoardWidthCm` | `140` / `42` | Board size (cm). |
| `BuoyancyN` | `1500` | Most the water can push up on the board (N). |
| `BuoyancyNaturalFrequencyHz` | `0.945` | How quickly the board bobs back to its ride height (Hz); the stiffness scales with the mass. |
| `BuoyancyDampingRatio` | `0.79` | Damping of that bob: 1 settles without overshoot. |
| `PlaningThresholdCmS` | `400` | Speed at which the board planes (cm/s, 7.8 kn). |
| `DisplacementDragKgPerS` | `8` | Linear drag below planing speed (kg/s). |
| `DisplacementQuadraticDragKgPerCm` | `0.1` | Quadratic drag below planing speed (kg/cm). |
| `PlaningDragKgPerS` | `8` | Linear drag on the plane (kg/s). |
| `PlaningQuadraticDragKgPerCm` | `0.03` | Quadratic drag on the plane (kg/cm); dominates, so speed scales with the wind. |
| `PlaningLiftKgPerS` | `50` | Upward lift per cm/s above planing speed (kg/s). |
| `BaseGripKgPerS` | `500` | Sideways grip with no edge input: fins and a neutral stance (kg/s). |
| `EdgeGripKgPerS` | `2000` | Extra sideways grip at full carve input (kg/s). |
| `EdgeDriveEfficiency` | `0.35` | Share of the grip force a heeled rail turns into forward drive. |
| `MaxEdgeAngleDeg` | `35` | Most roll and carve angle (deg). |
| `MaxBoardSpeedCmS` | `1800.4` (35 kn) | Speed clamp (cm/s). |
| `CarveTurnRate` | `60` | Turn rate at full carve once planing (deg/s). |
| `CarveResponse` | `6` | How quickly the carve follows the input (1/s). |
| `TailWeightGripScale` | `2.5` | Grip multiplier with the weight on the tail; the inverse on the nose. |
| `TailWeightDrag` / `NoseWeightDragSaving` | `0.35` / `0.15` | Planing drag added with the weight back, saved with it forward. |
| `TailWeightHeelDeg` | `12` | Extra heel with the weight on the tail (deg). |
| `WeightShiftPitchDeg` | `8` | Board pitch at full weight shift on the water (deg). |
| `AutoHeelDeg` / `AutoHeelFullLoadN` | `12` / `500` | Heel away from the kite at full sideways load (deg), and that load (N). |
| `LowSpeedPivotMaxSpeedCmS` / `LowSpeedPivotRate` / `LowSpeedPivotMinForceN` | `400` / `120` / `80` | Below this speed (cm/s) the board pivots towards a beam reach at this rate (deg/s) when the pull is over this (N). |
| `FloatSubmersionCm` / `FloatUntilSpeedFraction` / `FloatResponse` | `85` / `0.45` / `2.5` | Floating depth (cm), the fraction of planing speed below which the rider is fully sunk, and how fast they sink or rise (1/s). |
| `SwitchStanceSpeedCmS` | `100` | Tail-first speed at which nose and tail swap (cm/s). |
| `LiftoffWeightFactor` / `EdgedLiftoffWeightBonus` | `1.5` / `3.0` | Upward line force, in rider weights, that lifts a flat board off, and the extra at full edge. |
| `AirSpinRate` | `200` | Spin in the air at full carve (deg/s). |
| `RiderDragAreaM2` | `0.7` | Drag area of the rider and board in the air (m^2). |
| `MaxStepSeconds` / `MaxStepsPerUpdate` | `1/240` / `48` | Sub-steps of `Simulate` when nobody else steps the board. |

The kite's own tunables are on `UKiteComponent` (aerodynamics: `MaxLiftCoefficient` 1.2, `StallAngleDeg` 18, `ZeroLiftAngleDeg` 0, `ParasiteDragCoefficient` 0.09, `InducedDragFactor` 0.085, `SteeringDragFactor` 0.6, `SideForceCoefficient` 1.2, `TrimSheetedOutDeg` -22, `TrimSheetedInDeg` 2; steering: `MinTurnRadiusCm` 420 at 12 m^2, `TurnResponse` 9, `SteeringDeadTimeSeconds` 0.15, `DepoweredDeadTimeExtraSeconds` 0.3, `DepoweredTurnRateFactor` 0.45, `GravityTurnRadMPerS2` 2.4; lines: `LineLengthCm` 2400, `MaxLineTensionN` 6000). What to tune them towards is in `docs/physics/CHANGELOG.md`.

## Rider stance and harness
The rider's feet are in the straps, so the body always stands square across the board and turns with it: through carves, and through every spin in the air (`AKiteRiderPawn::UpdateRiderPose`). Which rail they face is chosen to face the kite when they get on the board (start, reset, or while floating). After that it is whichever rail keeps them facing the way they were, so a twin-tip swapping ends under them does not turn them round.

The lines pull on the harness hook at the front of the rider's waist (`HarnessHookOffsetCm`), and the bar rides on them just beyond it, further out the more it is sheeted out. The bar is always in front of the body. On the water a rider who ends up with their back to the kite slides the board round after `RiderSwitchDelaySeconds` and faces it again; in the air they are free to spin, and the lines come over their shoulder while their back is turned.

The lean is away from the kite's pull. On the water that is leaning out against it; in the air the rider hangs from the harness, so the lower the kite, the further back the shoulders go (up to `RiderAirHangLeanDeg`). Board-off tricks, where the feet leave the straps, are not modelled yet.

### Jointed body
Santa and the wetsuit rider are jointed figures: a torso and eight limb parts, posed every frame by `RiderRig` (`RiderRig.h`). The robot is still the skeletal mannequin.
- **Feet** are in the straps, 30 cm either side of the middle of the board along its length, and go wherever the board goes, tilt included.
- **Pelvis** is over the feet along the body's lean, 80 cm up standing and 40% lower in a full loaded crouch, and never further from a strap than the leg reaches.
- **Knees and elbows** come from a two-bone solve (`SolveTwoBone`) that keeps each bone its length: knees forwards and a little apart, elbows down and out.
- **Tricks:** the rig also takes a full body orientation (`FRiderRigInput::BodyQuat`, for rotations in the air). The torso, the pelvis line and the knee poles then come from the body itself, so an upside-down rider keeps the feet in the straps with the knees bending towards the chest. The pawn does not set it yet (the rider attitude will, in the air), so the pose is unchanged.
- **Hands** are on the bar 14 cm either side of its middle, so the arms follow the bar as it is sheeted and steered. The bar is kept within the arms' reach of the shoulders, so leaning back brings it in towards the hook.
- The parts' lengths and joint positions are shared with `generate_mesh_objs.py` (`RIDER_*`), which builds the meshes; `import_rider_parts.py` imports them.

## Gear
The gear screen (`UKiteSurfGearWidget`, opened by PLAY and by GEAR in the pause menu) sets the wind, the kite size and model, the board and the rider. The choices live in `UKiteSurfGameInstance`, are saved with the settings, and are applied when a ride starts (`AKiteSurfGameMode::InitializeRide`) or when the screen is confirmed during one.

- **Kite model** (`UKiteComponent::SetKiteModel`, traits in `KiteGear.cpp`): the loop kite (3 strut) is the kite the simulation is tuned for. The boost kite (5 strut) has 12% more lift, 15% less induced drag, 20% more mass and a 30% wider turn. Each released a little before its send would pull the rider off the edge in 30 kn (the boost kite turns slower and loads up later), the loop kite jumps about 12 m with 3.6 s in the air and the boost kite about 13 m with 3.8 s (`KiteSurf.Gear.ChangesBehaviour`). The loop kite turns about 355 degrees in the time the boost kite turns 255.
- **Board size** (`UBoardMovementComponent::SetBoardSize`): the 138 is the reference. The 132 has 20% more pop and turn rate, planes at 20% more speed, and has 10% less drag and 12% less grip. The 145 planes at 18% less speed with 15% more grip, and has 15% less pop and turn rate and 12% more drag. From a slow start in 12 kn the 145 gets up and planes while the 132 stays sunk.

The board's size does not change how it looks yet.

## The spot
`AKiteSurfSpot` is what is in the water. The game mode spawns it when a ride starts and lays it out round the start position in wind coordinates (across the wind is the way a ride starts out; down is downwind), so the layout is the same whatever the wind direction. The gear screen's SPOT toggles (saved with the settings) switch sandbars, islands and sharks on and off, and applying them during a ride changes the spot where it is.

- **Sand.** Sandbars and islands are the tops of flattened ellipsoids sunk a little below the water; `FSpotObstacle::GetSandHeightCm` gives the height of sand at a point from the same radii the meshes are built with (`generate_mesh_objs.py`). A sandbar's crest is 50 cm out of the water, 54 m long along the wind and 7 m across; an island stands 2.7 m high. There is no sand within `ClearStartRadiusCm` (150 m) of the start. A rider who is not above the sand where they are has run aground: a crash, and they are put back in the water on the side they came from.
- **Sharks** patrol a 40 m circle at 3.5 m/s. A rider who is floating or has crashed within `SharkNoticeRadiusCm` (90 m) is hunted at 6.5 m/s. Any rider within `SharkBiteRadiusCm` (2.6 m) of a shark and less than `SharkClearHeightCm` (80 cm) above the water is a crash, after which that shark leaves them alone for 8 s.
- The HUD shows what happened ("Ran aground", "Shark!") through `AKiteSurfHUD::ShowNotice`.

The sand is not yet part of the water surface the board rides on, and the sharks do not avoid the sand.

## Sound
Six loops play all the time and are faded and pitched by what the rider would hear (`AKiteRiderPawn::ComputeAudioMix`, from an `FRideAudioState`):

| Loop | Follows | Silent when |
| --- | --- | --- |
| Wind in the ears | apparent wind | there is none |
| Water under the board | board speed | the board is in the air |
| Spray | edge or load crouch, times board speed | in the air, standing still, riding flat |
| Lines singing | line tension | the lines are slack |
| Kite through the air | the kite's airspeed above about 16 m/s, so a turn or a loop roars | the kite is parked or down |
| Canopy flutter | a kite with no load in it: slack lines, a stall, or the bar right out | the kite is loaded, or there is no wind |

One-shots, each played with a few per cent of random pitch so that no two are alike: the pop, the landing (louder and deeper the harder it is), the crash, the kite hitting the water, the relaunch, the board running aground, a shark, and the reset. Running aground and a shark replace the splash of the crash they cause. The menus tick on moving, chime on choosing and fall on going back (`KiteSurfMenuStyle::PlayMenuSound`, driven by `FKiteMenuNavigator::OnAction`).

**Music.** The menus play `MU_Menu`. The ride plays two loops of exactly the same length, started on the same frame and never pitched, so they stay in step: `MU_RideBase` all the time, and `MU_RideAir` (the tune and a busier top end) faded in within half a second of leaving the water and out over about two seconds after landing. Music plays through a pause. 

**Volume settings.** Settings has four sliders, all saved and all heard as they are moved: MASTER VOLUME over everything, MUSIC VOLUME (default 60 %), AMBIENT VOLUME for the six loops above, and EFFECTS VOLUME for the one-shots and the menus' sounds. `ComputeAudioMix` is what the ride calls for; the ambient volume scales what the loops then play.

The effects are synthesised by `scripts/editor/make_sound_wavs.py` and the music is written out as notes and synthesised by `scripts/editor/make_music_wavs.py`; both are standard-library Python, and `make_sound_assets.py` imports the lot. The loops are set to keep playing while silent so they come back, in step, after being faded out.

To hear what a scripted run sounds like without speakers, record it: `kitesurf.AudioRecordStart`, then `kitesurf.AudioRecordStop <name>` writes `Saved/BouncedWavFiles/<name>.wav`. An offscreen run is muted as an unfocused window unless it is started with `-ini:Engine:[Audio]:UnfocusedVolumeMultiplier=1.0`.

## Motion bar
Behind the MOTION BAR setting (`UKiteSurfGameInstance::bMotionBar`, off by default), `AKiteRiderPawn` takes the bar from a controller's motion sensors instead of the right stick.

- **Reading.** `IKiteMotionSource` gives accelerometer (g) and gyro (rad/s) readings in the controller's axes. On Linux `KiteMotionBar::CreatePlatformSource` reads them through the SDL instance the engine already runs (it opens the pads but does not pass motion on); the module includes SDL's headers and uses the symbols `ApplicationCore` exports, without linking a second copy. Other platforms return no source.
- **Attitude.** `FMotionBarFilter` keeps an estimate of which way is up in the controller's axes: the gyro turns it at once, and the accelerometer pulls it back to true over `AccelTimeConstantSeconds` whenever it reads within `AccelTrustBandG` of 1 g. Roll is right side down; pitch is the pad pulled in like a bar (the edge nearest the player rising, which on a DualSense is "up" gaining a part along the controller's +Z), measured so that it does not change with roll.
- **Bar.** `FMotionBarMapping`: roll beyond a 3 degree deadzone steers, full at 35 degrees; pitch moves the bar through its throw over 50 degrees. Switching on, a reset, or the controller reconnecting calibrates: the way it is held then is level, with the bar where it was.
- **Hand-over.** While a reading is available the stick, triggers and keys do not move the bar's position and the right stick is taken out of the steering (the steering keys and the mouse still add). If the controller goes away the bar is handed back level.

The right stick is left unused while the motion bar is active. `kitesurf.MotionBar <0|1>` switches it from the console and logs what is being read.

## Vibration
`AKiteRiderPawn::PlayHaptic` asks the player's controller for one short buzz (`PlayDynamicForceFeedback`), behind the VIBRATION setting (`UKiteSurfGameInstance::bHaptics`, on by default). It is used for: the pop (a light tick, 0.1 s); a landing (`GetLandingHaptic`: 0.3 strength for a soft one up to full at 5 g, 0.12 to 0.32 s); a crash of any kind (full, 0.45 s); the kite hitting the water (0.6, 0.25 s); and a yank on the lines, once as the tension rises through `HapticYankTensionN` (2200 N) and not again for 0.8 s.

## Bar display
The HUD draws the control bar next to the power gauge (`AKiteSurfHUD::DrawControlBar`). The bar slides down its throw as it is pulled in and tilts towards the hand that is pulling, whichever device is driving it (arrow keys, mouse with the right button held, right stick). The lines change colour with the load in them and go dull when slack; the bar lights up while the loop input is held. The scale underneath shows the rider's steering as a filled bar and, as a marker, the steering that actually reaches the kite (`UKiteComponent::GetAppliedSteer`): the two differ while the assist is flying the kite, and while looping they match once the bar has had its dead time to reach the kite.

## Safety Guards & Telemetry
- **NaN / Inf Guard**: `ensureAlwaysMsgf(!Velocity.ContainsNaN(), ...)` in `AKiteRiderPawn::Tick` and `UBoardMovementComponent::StepBoard`. The scenario tests check every frame that nothing goes NaN, the kite is never past line length and the tension never negative (`PhysicsScenarioTests.cpp`).
- **Debug view**: `kite.Physics.Debug 1` draws the winds and forces at the kite and the rider, `2` also logs a `kitecsv` line per step (`docs/ARCHITECTURE.md`).
- **Water Surface Clamp**: Board location Z is constrained within $\pm 20$ cm of water height while planing.
- **HUD Telemetry**: `AKiteSurfHUD` renders normalized $0-360^\circ$ heading alongside knots speed.
