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

### Floating and the water start
The board only carries the rider at speed. Below `FloatUntilSpeedFraction` of the planing threshold the rider floats with the board `FloatSubmersionCm` under the surface (about chest deep) and lies back in the water; between there and the planing threshold they rise, and at planing speed the board rides on the surface. The depth follows the speed at `FloatResponse`, so a rider who loses the kite sinks over a second or so and comes back up as the kite gets them going again. A rider in the air lands on the surface first and sinks from there. `IsFloating()` and `GetFloatDepthCm()` report it; the HUD state line reads Floating, Getting up or Planing.

### Course keeping, edging and carving
- **Lateral Resistance**: $F_{lat} = (C_{lat,base} + C_{edge} \cdot |\text{EdgeInput}|) \cdot v_{lat}$, capped at $m / \Delta t$ so one step can at most cancel the sideways speed. The base term is the fins and a neutral stance: with no edge input the board holds its heading against the kite's sideways pull.
- **Edge Drive**: Hydrodynamic lift along the board rail converts lateral holding force into forward thrust: $F_{fwd} = |F_{lat}| \cdot |\text{EdgeInput}| \cdot \eta_{edge}$.
- **Turning** (A / D, left stick X): A turn input carves the board: its heading moves towards $v_{heading} + \text{EdgeInput} \cdot \text{MaxEdgeAngleDeg}$ at up to `CarveTurnRate`. Holding an edge keeps turning, so a steady course is ridden with the edge neutral.
- **Switching stance**: A twin-tip rides either way. When the board is moving tail-first faster than `SwitchStanceSpeedCmS`, nose and tail swap, so after flying the kite to the other side the rider simply rides off on the new tack.
- **Load heel**: The board heels away from the kite by up to `AutoHeelDeg` in proportion to the sideways line force.

### Weight shift, liftoff and the air
- **Weight shift** (W / S, left stick Y): +1 is weight on the nose, -1 on the tail. Weight back sinks the tail: lateral grip is scaled by `TailWeightGripScale`, drag rises by `TailWeightDrag`, the board heels further, the nose lifts, and the pop gains `TailWeightPopBonus`. Weight forward flattens the board: grip drops by the same factor, planing drag falls by `NoseWeightDragSaving`, and the nose dips. In the air it tips the board by up to `AirWeightShiftPitchDeg`.
- **Off the plane** the board pivots towards a beam reach on the kite's side at up to `LowSpeedPivotRate`, so a stalled rider is lined up for the kite to pull them back onto the plane.
- **Liftoff**: the board leaves the water when the kite's upward pull exceeds `LiftoffWeightFactor` times the rider's weight, or up to `EdgedLiftoffWeightBonus` more when the rider is edging. Holding the edge while the kite is sent, then popping, is how a jump is loaded; see `docs/jumping.md`.
- **In the air** the carve input spins the board at `AirSpinRate`; left alone it comes back in line with the direction of travel. A twin-tip lands either way round, so only the angle to the board's axis decides between a clean landing and a crash. A skip shorter than 0.25 s and lower than 30 cm is not counted as a jump.

### How the kite drives the board
The kite (`UKiteComponent`) is a point mass on the end of its lines, stepped at 240 Hz or faster in SI units.

- **Forces on it**: lift and drag from the air flowing over it (true wind minus its own velocity), a side force that resists sliding sideways, gravity, and the pull of the lines.
- **Angle of attack** is the angle of that airflow to the canopy plus the trim from the bar (`TrimSheetedOutDeg` to `TrimSheetedInDeg`). Lift rises to `MaxLiftCoefficient` at `StallAngleDeg`; past that the kite is stalled and makes mostly drag. Air on the wrong side of the canopy (overflying the window, or a fully depowered kite flown fast) luffs it.
- **The lines only pull.** They hold the kite at line length while the forces on it point away from the rider; the tension is whatever that takes, so it rises with the square of the kite's airspeed and falls to nothing in a lull. When it reaches zero the lines are slack: the kite stops flying and falls like a sheet (`SlackDragCoefficient`) until the lines snatch tight again or it reaches the water.
- **Parking** is not scripted: with its nose out of the window the kite settles where lift, drag and line tension balance, about 10 degrees inside the window edge, and further back the faster the rider goes. That is what limits board speed and upwind angle.
- **Steering** turns the nose at airspeed / `MinTurnRadiusCm`, wound in at `TurnResponse`. There is no loop key. Bar towards the other side of the window is a request to fly there: an assist takes the kite over the top, and turns the nose up before it reaches the water. Bar centred means hold this clock position (the assist leans the nose against gravity and gusts). Bar towards the side the kite is already on, once it is more than `LoopClockDeg` round that side, goes straight to the kite: it turns down and round in a loop for as long as the bar is held, and a loop taken too low goes into the water. Reversing or centring the bar ends the loop.
- **In the water** the lines are slack; the kite relaunches after `RelaunchDelaySeconds`, or sooner if steered, provided there is wind to fly in.
- The pull passed to the rider is capped at `MaxLineTensionN`.
- **Size**: `SetKiteSize` rigs a kite of a given area and scales its mass, the air it has to push and its turning radius with it (12 m^2: 3 kg, 4.2 m radius). `RecommendKiteSizeM2` gives the size a rider would rig for the wind, about 2.2 x rider kg / knots, from the sizes on offer (5 to 17 m^2). The game rigs that size unless one is chosen in Settings; the HUD shows it next to the wind.
- The bar position is persistent: sheet input moves it at `SheetRatePerSec` and it stays there.
- A ride starts on a beam reach at 12 kn with the kite at clock 65 on that side (`AKiteSurfGameMode::InitializeRide`). In steady 15 kn wind with no input the board settles at about 15 kn with about 500 N in the lines; pointed 30 degrees above a beam reach it gains about 2.2 m/s against the wind.

### Wind
`UWindComponent` adds gusts, direction drift and shear to the base wind. Gusts are a slow swell over `GustPeriodSeconds` with quicker puffs on top (`GustPuffRate`, `GustPuffShare`), reaching most of `GustStrength` either way. `GetGustFactorAt` gives the current wind over the base wind; the HUD calls out GUST and LULL from it.

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
| `EdgeGripCoef` | `2000.0f` | Extra lateral grip at full turn input. |
| `EdgeDriveEfficiency` | `0.35f` | Forward drive efficiency gained from rail edging. |
| `MaxEdgeAngleDeg` | `35.0f` | Maximum board roll and yaw carve angle. |
| `MaxBoardSpeed` | `1800.4f` (35 kn) | Velocity magnitude clamp in cm/s. |
| `CarveTurnRate` | `60.0f` | Turn rate at full turn input once planing, in degrees per second. |
| `CarveResponse` | `6.0f` | How quickly the carve follows the input (1/s). |
| `TailWeightGripScale` | `2.5f` | Grip multiplier with the weight on the tail; inverse on the nose. |
| `WeightShiftPitchDeg` | `8.0f` | Board pitch at full weight shift on the water. |
| `LowSpeedPivotRate` | `120.0f` | Pivot rate towards a beam reach when stopped, in degrees per second. |
| `LiftoffWeightFactor` | `1.5f` | Upward line force, in rider weights, that lifts the board off. |
| `AirSpinRate` | `200.0f` | Board spin rate in the air at full carve, in degrees per second. |
| `SwitchStanceSpeedCmS` | `100.0f` | Tail-first speed at which nose and tail swap. |
| `AutoHeelDeg` | `12.0f` | Heel away from the kite at full sideways load. |
| `BuoyancySpringStiffness` | `3000.0f` | Vertical water surface equilibrium spring constant. |
| `BuoyancyDamping` | `800.0f` | Vertical damping constant. |
| `PlaningLiftCoef` | `50.0f` | Planing hydrodynamic lift force coefficient. |

## Rider stance and harness
The rider's feet are in the straps, so the body always stands square across the board and turns with it: through carves, and through every spin in the air (`AKiteRiderPawn::UpdateRiderPose`). Which rail they face is chosen to face the kite when they get on the board (start, reset, or while floating). After that it is whichever rail keeps them facing the way they were, so a twin-tip swapping ends under them does not turn them round.

The lines pull on the harness hook at the front of the rider's waist (`HarnessHookOffsetCm`), and the bar rides on them just beyond it, further out the more it is sheeted out. The bar is always in front of the body. On the water a rider who ends up with their back to the kite slides the board round after `RiderSwitchDelaySeconds` and faces it again; in the air they are free to spin, and the lines come over their shoulder while their back is turned.

The lean is away from the kite's pull. On the water that is leaning out against it; in the air the rider hangs from the harness, so the lower the kite, the further back the shoulders go (up to `RiderAirHangLeanDeg`). Board-off tricks, where the feet leave the straps, are not modelled yet.

## Gear
The gear screen (`UKiteSurfGearWidget`, opened by PLAY and by GEAR in the pause menu) sets the wind, the kite size and model, the board and the rider. The choices live in `UKiteSurfGameInstance`, are saved with the settings, and are applied when a ride starts (`AKiteSurfGameMode::InitializeRide`) or when the screen is confirmed during one.

- **Kite model** (`UKiteComponent::SetKiteModel`, traits in `KiteGear.cpp`): the loop kite (3 strut) is the kite the simulation is tuned for. The boost kite (5 strut) has 12% more lift, 15% less induced drag, 20% more mass and a 30% wider turn. With the best timing for each in 30 kn, the loop kite jumps about 15 m with 4.3 s in the air and the boost kite about 17.5 m with 4.9 s; the loop kite turns about 500 degrees in the time the boost kite turns 380.
- **Board size** (`UBoardMovementComponent::SetBoardSize`): the 138 is the reference. The 132 has 20% more pop and turn rate, planes at 20% more speed, and has 10% less drag and 12% less grip. The 145 planes at 18% less speed with 15% more grip, and has 15% less pop and turn rate and 12% more drag. From a slow start in 12 kn the 145 gets up and planes while the 132 stays sunk.

The board's size does not change how it looks yet.

## Sound
Three loops play all the time and are faded and pitched by what the rider would hear (`AKiteRiderPawn::ComputeAudioMix`): wind in the ears from the apparent wind, water under the board from board speed (silent in the air), and the lines singing from line tension (silent when slack). The pop, landing (louder and deeper the harder it is), crash and reset are one-shots. All of it is synthesised by `scripts/editor/make_sound_wavs.py` and imported by `make_sound_assets.py`; the loops are set to keep playing while silent so they come back after being faded out.

To hear what a scripted run sounds like without speakers, record it: `kitesurf.AudioRecordStart`, then `kitesurf.AudioRecordStop <name>` writes `Saved/BouncedWavFiles/<name>.wav`. An offscreen run is muted as an unfocused window unless it is started with `-ini:Engine:[Audio]:UnfocusedVolumeMultiplier=1.0`.

## Bar display
The HUD draws the control bar next to the power gauge (`AKiteSurfHUD::DrawControlBar`). The bar slides down its throw as it is pulled in and tilts towards the hand that is pulling, whichever device is driving it (arrow keys, mouse with the right button held, right stick). The lines change colour with the load in them and go dull when slack; the bar lights up while the loop input is held. The scale underneath shows the rider's steering as a filled bar and, as a marker, the steering that actually reaches the kite (`UKiteComponent::GetAppliedSteer`): the two differ while the assist is flying the kite and match while looping.

## Safety Guards & Telemetry
- **NaN / Inf Guard**: `ensureAlwaysMsgf(!Velocity.ContainsNaN(), ...)` in `AKiteRiderPawn::Tick` and `UBoardMovementComponent::TickComponent`.
- **Water Surface Clamp**: Board location Z is constrained within $\pm 20$ cm of water height while planing.
- **HUD Telemetry**: `AKiteSurfHUD` renders normalized $0-360^\circ$ heading alongside knots speed.
