# Physics review

Phase 1 of the physics rework. Read on branch `physics/rework` at `3f3c389` (the bar-throttle
commit on top of `main`). Nothing in this document changes code; it maps what exists and ranks
what is wrong with it. Line numbers refer to that revision.

Verification used for the numbers quoted here: `scripts/build.sh Development` (94 s, clean) and
`scripts/run-tests.sh -nullrhi` (`Test Results: Total=73, Succeeded=73, Failed=0`). Everything
measured below comes from what those tests log; nothing was seen in a `-game` run.

## Summary

All of the physics is custom C++ in one module. There are no Chaos rigid bodies and no Blueprint
physics; the Blueprint pawn only assigns input assets and camera lag
(`scripts/editor/make_input_assets.py:131-171`). The kite is the strongest part: a point mass on
a line sphere, stepped in SI at 240 Hz or faster, with lift, drag, stall, a slack state and an
analytic line tension. The board is the weakest: a point mass integrated once per frame with
explicit Euler, with edging modelled as viscous lateral drag plus a thrust heuristic, and with
its orientation set kinematically. Wind is a stateless Perlin function of position and world
time: no seed, no travelling gusts, and the shear profile stops growing at 10 m, so the kite at
24 m sees no more wind than the HUD reports at 10 m.

The three problems that matter most for "believable, responsive, stable and tunable" are:

1. the board runs at the frame step and its grip is capped by the frame step, so riding
   changes with frame rate (P1);
2. the order in which pawn, kite and board tick is not pinned, and differs from the order
   the tests use (P2);
3. the wind is not a field the whole game can sample deterministically, and it has no
   gust fronts or realistic profile (P3).

Everything else is calibration and tuning pain rather than structure.

## 1. Where physics lives

| Question | Answer | Evidence |
| --- | --- | --- |
| C++ or Blueprint | All C++. `BP_KiteRider` and `BP_KiteSurfGameMode` are thin subclasses that set input assets, camera lag and the pawn class | `scripts/editor/make_input_assets.py:131-191` |
| Chaos or custom | Custom. The pawn root is a static mesh moved with `SafeMoveUpdatedComponent` (sweep for blocking hits only, no simulation). The kite mesh has no collision | `BoardMovementComponent.cpp:597`, `KiteComponent.cpp:155` |
| Fixed or variable step | Variable. Board: one explicit Euler step per frame. Kite: the frame is split into `ceil(dt * 240)` equal sub-steps, at most 24, so the sub-step length varies with frame rate and grows past 1/240 s when a frame exceeds 100 ms. No accumulator, no render interpolation | `BoardMovementComponent.cpp:488`, `KiteComponent.cpp:17-18,852-853` |
| Tick groups | Kite `TG_PrePhysics` with the wind component as prerequisite; board `TG_PrePhysics` (inherited from `UMovementComponent`); pawn actor tick default (`TG_PrePhysics`); wake `TG_PostPhysics` after the board | `KiteComponent.cpp:52,131`, engine `MovementComponent.cpp:28`, `BoardWakeComponent.cpp:22,48` |
| Tick order pawn / kite / board | Not pinned. Nothing declares a prerequisite between them, and the 5.8.3 engine does not make an actor's tick a prerequisite of its components (`ActorComponent.cpp` only adds prerequisites through `AddTickPrerequisite*`). The tests step `Kite -> Pawn -> Board` by hand | `KiteRiderPawn.cpp:24,431`, `RideLoopTests.cpp:71-73` |
| Async physics | Not used. No `bTickPhysicsAsync`, no sub-stepping config in `Config/` | `Config/DefaultEngine.ini` |
| Frame-time clamp | None in the project; engine default `MaxDeltaTime=0` | engine `BaseEngine.ini:1581` |

## 2. Component map

| File / class | Function | Owns | Units inside | Step |
| --- | --- | --- | --- | --- |
| `WindComponent.cpp` `UWindComponent` | `GetWindAt(pos)`, `GetGustFactorAt(pos)` | Base wind vector, gust and drift parameters, shear height | cm/s; knots at the boundary | Stateless; samples world time |
| `KiteWindMath.cpp` `UKiteWindMath` | `KnotsToCmPerSec`, `ApparentWind`, `WindWindowAzimuthDeg`, `KitePositionInWindow` | Nothing | cm/s | Pure functions |
| `KiteComponent.cpp` `UKiteComponent` | `UpdateKite(dt)` -> `ComputeSteering` (assist) + N x `StepFlight` (flight), `GetAeroCoefficients`, `Crash`, `Relaunch`, `UpdateVisuals` | Kite position, velocity, heading, sheet, steer, tension, taut/slack/crashed state, mesh and cable visuals | SI inside `StepFlight`; cm at the API | Sub-steps of at most 1/240 s inside a frame |
| `BoardMovementComponent.cpp` `UBoardMovementComponent` | `TickComponent` (all board and rider dynamics), `Jump`, `TriggerCrash`, `ResetToTack`, `SampleWaterSurface` | Rider+board point mass velocity, drag regime, board state machine, jump telemetry, water surface pointer | cm, kg, s; forces in kg*cm/s^2; coefficients in unlabelled mixed units | One frame step |
| `KiteRiderPawn.cpp` `AKiteRiderPawn` | `Tick`: hands the line force to the board, integrates the bar position, rider pose, bar ends, camera, audio | Input state, bar position, rider stance, harness hook position (visual) | cm | Frame |
| `KiteWaterSurface.cpp` `IKiteWaterSurface` | Flat, sine-wave and Water-plugin implementations of height and normal at an XY point | Water body pointer | cm | On demand |
| `KiteSurfGameMode.cpp` | `InitializeRide`: sets base wind from the game instance, rigs kite and board, places the kite, sets start velocity | Start conditions | knots -> cm/s | Once |
| `KiteGear.cpp` | Trait tables for kite model and board size | Scale factors | dimensionless | Static |
| `KiteSurfHUD.cpp`, `BoardWakeComponent.cpp`, `KiteSurfSpot.cpp` | Consumers: wind at +10 m, gust factor, tension, speed, state; sharks and sand call `TriggerCrash` | | | Frame |

## 3. Data flow per frame

With the order the tests use (kite, pawn, board). In the game the pawn and board may run before
the kite (P2).

1. **Wind** has no tick. Every caller evaluates `GetWindAt` directly: Perlin gust and drift from
   `(world time / period, x * 1e-4, y * 1e-4)`, a shear factor from `Z / ShearHeightCm`, scaled
   onto `BaseWind` (`WindComponent.cpp:42-86`).
2. **Pawn tick** (`KiteRiderPawn.cpp:423-461`): `BoardMovement->AddExternalForce(Kite->GetLineForce())`
   with whatever the kite computed last (`:431`); bar position `+= rate * SheetRatePerSec * dt`
   (`:436`); mouse bar; rider pose and harness hook (`:503-622`); camera; audio from apparent
   wind at the pawn, board speed and tension (`:729-759`).
3. **Kite tick** (`KiteComponent.cpp:804-896`): if crashed, lie on the water and count down to
   relaunch (`:814-841`). Otherwise sample the wind once at the kite (`:843`), run the assist
   controller once with the frame dt to turn the bar into a steering command (`:844`), then
   sub-step `StepFlight` with the rider position interpolated from last frame to this one
   (`:858-863`). Each sub-step: airflow = wind - kite velocity (`:669`); angle of attack from the
   flow against the canopy plane plus bar trim (`:683-684`); lift, drag, side force and weight
   (`:694-697`); tension needed to hold the kite on the sphere, including the centripetal term
   (`:704`); taut if at line length and tension positive, else slack and the canopy collapses
   towards a sheet over `SlackCollapseCm` (`:705-739`); heading rate from steer, weathercock and
   gravity, lagged by `TurnResponse` (`:749-766`); semi-implicit Euler (`:769-770`); projection
   back onto the sphere and removal of radial relative velocity (`:772-798`). After the loop the
   mean tension is capped at `MaxLineTensionN` and turned into a force along the line (`:882-883`).
4. **Board tick** (`BoardMovementComponent.cpp:280-703`): crash deceleration and respawn
   (`:292-317`); landing timer (`:320-336`); one water sample at the pawn XY (`:338-341`);
   floating depth lerp (`:348-350`); airborne bookkeeping and the 40 m ceiling (`:355-372`);
   force sum = accumulated external force + gravity (`:375-382`); kite lift-off test against
   `1.5 .. 4.5` body weights depending on edge hold (`:388-397`); buoyancy spring clamped to
   `0..1500 N` when within 15 cm of the ride height (`:399-414`); regime switch at 400 cm/s
   (`:421-436`); displacement or planing drag, planing lift (`:438-463`); lateral viscous grip
   capped at `m/dt`, and edge drive = |lateral drag| x edge x 0.35 (`:465-482`); `v += F/m dt`
   and a 35 kn clamp (`:485-497`); orientation set kinematically from water normal, load heel,
   carve and weight shift, or spun in the air (`:502-592`); `SafeMoveUpdatedComponent`
   (`:594-602`); landing detection, 80% speed retention or crash (`:606-678`); hard clamp to
   within 20 cm of the ride height with `Vz = 0` (`:680-699`).
5. **Wake** lays foam and spray from the new board position (`TG_PostPhysics`).
6. **HUD** reads speed, state, tension, wind at the pawn + 10 m and the gust factor
   (`KiteSurfHUD.cpp:253-270,414,498`).

## 4. The models

### 4.1 Kite

- **Lift and drag** (`KiteComponent.cpp:550-574`). Attached flow: `Cl = ClMax * sin(pi/2 * alpha /
  alphaStall)`, `Cd = Cd0 + k * Cl^2`, with `ClMax 1.2`, `alphaStall 18 deg`, `Cd0 0.09`,
  `k 0.085` (`:63-67`). Past the stall it blends over 5 deg into a flat plate with a `1.8 sin(alpha)`
  normal force tilted 10 deg towards the nose. Peak L/D is 1.2 / (0.09 + 0.085 * 1.44) = 5.7.
  `Cl(0) = 0`; a cambered tube kite has about 0.2 at zero angle (see the research doc).
- **Angle of attack** is the angle of the airflow to the plane perpendicular to the lines, plus a
  trim from the bar (`-22 .. +2 deg`, `:72-73`). This is a model convention, not the chord
  angle a wind-tunnel number refers to; the trim range is the real tuning knob.
- **Apparent wind** uses the kite's own velocity (`:669`), so a diving kite pulls harder than a
  parked one. Good. The wind is sampled once per frame at the kite's start position (`:843`).
- **Lines** are a unilateral distance constraint: analytic tension from the force balance plus
  `m v_t^2 / L` (`:704`), position projected onto the sphere after the step (`:779`), radial
  relative velocity removed (`:780`). Tension never goes negative; slack lines let the canopy fall
  as a sheet (`:717-739`) and snatch tight at full length (`:783-794`). Line stretch is not
  modelled; `MaxLineTensionN = 6000` stands in for it (`:87`, `:882`).
- **Steering** (`:749-766`): turn rate = `steer * chordwise airspeed / MinTurnRadiusCm`
  (4.2 m for 12 m^2, scaled with the square root of area), plus a weathercock term and a gravity
  term, lagged through `FInterpTo(.., TurnResponse = 9)`. That is `g_k = 1/4.2 m = 0.24 rad/m`,
  in the measured range for a 9 to 12 m^2 kite. There is no steering dead time (the 0.11 s lag
  is the nearest thing), no reduction of turn rate when depowered, and steering drag grows with
  `steer^2` rather than linearly. The gravity term is `0.15 * g / v`, about a quarter of the
  measured `c2` for a 10 m^2 kite.
- **Assist** (`:576-657`): bar over means "fly round the window at `TravelHeadingDeg`", bar
  centred means "hold this clock position", and a floor rule turns the nose up 1.5 s before the
  kite would reach `MinElevationDeg`. Bar towards the kite's own side beyond `LoopClockDeg` goes
  straight to the kite and loops it. This controller runs once per frame with the frame dt
  inside the physics component.
- **Crash and relaunch** are scripted: below 60 cm world Z the kite is put on the water
  (`:865-868`, `:510-525`) and after 3 s, or when steered, it is teleported to
  `MinElevationDeg` nose-out (`:527-535`).
- **Size and model** (`:395-418`): mass scales with area, added mass with area^1.5, turn radius
  with the square root of area; the boost kite is a trait table (`KiteGear.cpp:285-298`).

### 4.2 Board and water

- **Vertical axis** is mostly kinematic. A buoyancy spring (`3000 kg/s^2`, `800 kg/s` damping,
  clamped to `0..1500 N`, active within 15 cm of the ride height, `:399-414`), a planing lift
  of `50 * (v - 400 cm/s)` kg*cm/s^2 (`:459`), and then a hard clamp to 20 cm around the ride
  height with `Vz = 0` (`:685-690`). The ride height itself is the water height minus a floating
  depth that is lerped from speed (`:348-350`). Wave orbital velocity is requested from the
  Water plugin and discarded (`KiteWaterSurface.cpp:28-33`).
- **Water sampling**: one point at the pawn XY, with waves included (`KiteWaterSurface.cpp:31`).
  The normal sets pitch and roll (`:531-534`); there is no multi-point fit, so a 1.4 m board on
  1 m chop takes the slope at one point.
- **Forward drag**: displacement `0.1 v^2 + 8 v`, planing `8 v + 0.03 v^2` (kg*cm/s^2 with v in
  cm/s), switched at 400 cm/s with no hysteresis (`:421-455`). At the switch the drag drops from
  191 N to 80 N, which snaps the board onto the plane. Weight on the tail adds 35%.
- **Lateral grip** is viscous: `F = (500 + 2000 |edge|) * 2.5^(-weight) * v_lat`, in kg/s
  (`:468-474`). Full edge with weight back is 6250 kg/s on an 85 kg mass, a 73 Hz rate, so the
  code caps the coefficient at `m / dt` (`:472`). At 60 fps that cap is 5100 and is active; at
  120 fps it is not. Grip therefore depends on frame rate.
- **Edge drive**: forward thrust = |lateral drag force| x |edge| x 0.35 (`:479`). This is where
  upwind ability comes from. It has no energy source in the model; it is a stand-in for the
  lift a heeled rail makes.
- **Heading** is set, not integrated: the yaw target is the velocity heading plus
  `carve * 35 deg`, approached at `60 deg/s * |carve|` (`:552-559`); the velocity then follows
  through the lateral drag. Roll is water slope plus up to 12 deg of load heel plus `carve * 35 deg`
  (`:540-542`). So the edge angle is cosmetic; what grips is the carve input and the weight shift.
- **Lift-off** when the external vertical force exceeds `(1.5 + 3.0 * edgeHold)` body weights
  (`:388-397`). Edging in this model holds the rider down against vertical pull; in reality
  edging resists the horizontal pull and so keeps the kite at the window edge where its vertical
  pull is smaller. The effect is similar; the mechanism is not.
- **Pop** (`:168-218`): `Vz += (21000 kg*cm/s * (1 + 0.5 tailWeight) + 0.22 s * F_kite,z) / m`.
  The second term is a pseudo-impulse (a force times an arbitrary 0.22 s). After take-off the
  line force keeps acting on the point mass, so the kite does carry the rider.
- **Landing** (`:642-676`): `Vz` is zeroed and 20% of horizontal speed removed in one frame; the
  broadcast "landing g" is `|Vz| / 980`, a velocity over an acceleration, so a time in seconds
  mislabelled as g. A landing angle over 30 deg between velocity and board axis is a crash.
- **In the air**, buoyancy, drag and grip are gated off above 10 cm (`:353`), carve spins the
  board at 200 deg/s (`:509-512`), and the ceiling is 40 m (`:365-371`).

### 4.3 Rider

- The rider is part of the 85 kg point mass. The line force is applied at the centre of mass;
  the harness hook offset exists only for the visuals (`KiteRiderPawn.cpp:593`). There is no body
  drag (0.5 to 1 m^2 CdA is 50 to 270 N at riding and loop airspeeds), no lean dynamics, and no
  rider rotation. In the air the point mass does swing under the kite, which is the pendulum.
- Inputs: steer (bar) and sheet rate go to the kite; carve (edge) and weight shift go to the
  board; jump pops. The bar position persists and moves at 2.5 throws per second (`:436`).

### 4.4 Wind

- `GetWindAt` (`WindComponent.cpp:42-86`): two Perlin octaves in `(t / 8 s, x, y)` with the
  spatial coordinate scaled by 1e-4 per cm (one noise unit per 100 m) for gusts, one for
  direction drift up to 10 deg, and a shear factor `0.7 + 0.3 * log10(1 + 9 z / 10 m)` clamped to
  `0..1`.
- Consequences: the field does not advect (a gust is a stationary blob that fades in and out, not
  a front crossing the water); the kite at 24 m sees exactly the 10 m wind; the pawn at Z = 0
  reads 0.7 of it, while the HUD samples at +10 m (`KiteSurfHUD.cpp:267`) and the audio at the
  pawn (`KiteRiderPawn.cpp:732-733`), so three different numbers are "the wind at the rider".
  `GustNoiseGain = 1.4` is applied after the clamp (`:65`), so the gust factor can exceed
  `1 +- GustStrength` when both octaves peak. Mean wind direction is hardcoded to +X by the
  game mode (`KiteSurfGameMode.cpp:33`) and by the console command (`KiteSurf.cpp:82`).
- Determinism: the time source is world time (`:53`); there is no seed and no `Time` argument,
  so the same ride cannot be replayed, and a test can only control time when the component has
  no world (`TimeOverride`). Each pawn owns its own wind component, so two pawns would have two
  winds.
- Apparent wind is used at the kite (`KiteComponent.cpp:669`), for the window axis
  (`:366-372`) and for the audio (`KiteRiderPawn.cpp:733`). The board and rider have no
  aerodynamic forces, so nothing else needs it yet.

### 4.5 Units, frames and time step

Unreal frame: cm, Z up, left-handed, yaw positive clockwise seen from above. Wind blows towards
+X by default; "right" in the window is `Up x Downwind`.

Places where units, frames or dt are mixed or hardcoded:

| Where | What |
| --- | --- |
| 20 source files, 25 non-test occurrences | `51.44` written out instead of one constant (`KiteWindMath.cpp:4`, `WindComponent.cpp:30`, `BoardMovementComponent.cpp:26,179`, `KiteSurfGameMode.cpp:12,33`, `KiteSurfHUD.cpp:95,100`, `KiteRiderPawn.cpp:733`, `KiteSurf.cpp:82`, UI widgets, tests) |
| `BoardMovementComponent.h:337` | `GetMaxBoardSpeedCmS` guesses whether `MaxBoardSpeed` is knots or cm/s from its magnitude |
| `KiteComponent.cpp:14` vs `BoardMovementComponent.cpp:380` | Kite uses `9.81 m/s^2`; board uses `GetGravityZ()` from world settings. Change one and the two disagree |
| `KiteComponent.cpp:883`, `BoardMovementComponent.cpp:396,410`, `KiteRiderPawn.cpp:562`, tests | `1 N = 100 kg*cm/s^2` conversions by hand |
| `BoardMovementComponent.cpp:16-67` | Coefficients in kg/s (`PlaningDragCoef`, `BaseLateralDragCoef`, `EdgeGripCoef`, `PlaningLiftCoef`), kg/cm (`PlaningQuadraticDragCoef`, `DisplacementDragCoef`), kg/s^2 (`BuoyancySpringStiffness`), kg*cm/s (`BaseJumpImpulse`), kg*cm/s^2 (`AutoHeelFullLoadForce`, `LowSpeedPivotMinForce`), seconds (`KiteLiftFactor`); none say so in the name |
| `BoardMovementComponent.cpp:659` | "LandingG" = cm/s divided by cm/s^2 |
| `BoardMovementComponent.cpp:472` | Grip coefficient capped by `MassKg / DeltaTime`: dt in the force law |
| `BoardMovementComponent.cpp:557` | Carve rate scaled by `clamp(speed / 400, 0.5, 1)`: the planing threshold used as a rate scale |
| `KiteComponent.cpp:852` | Sub-step length `dt / ceil(dt * 240)` varies with frame rate; past 24 steps the sub-step grows |
| `KiteComponent.cpp:576-657` | The assist uses the frame dt (`CentredBarSeconds`), not the sub-step |
| `KiteComponent.cpp:865`, `:643` | Kite crash and floor heights measured from world Z = 0 and the pawn Z, not the local water height |
| Literals in force code | `1.8` plate force (`KiteComponent.cpp:564`), `5 deg` stall blend (`:570`), `50 cm/s` (`:610`), `1.5 s` look-ahead (`:644`), `4/s` heading relax (`:737`), `3 m/s` (`:755`), `10 cm` airborne gate (`BoardMovementComponent.cpp:353`), `-15 cm` buoyancy gate (`:403`), `15 cm` contact falloff (`:458`), `20 cm` clamp (`:685`), `50 cm` minimum jump (`:624`), `0.25 s` landing timer (`:657`), `4.0` pitch and roll rates (`:524-525`), `60000` lean force (`KiteRiderPawn.cpp:562`), `40 kn` audio scale (`:713`) |

## 5. Ranked problems

Severity: **structural** (changes results everywhere), **behaviour** (wrong or unconvincing
motion), **tuning** (hard to adjust safely). Each has evidence and the symptom a player or a
test would see.

### P1. Board integration is frame-step dependent (structural)

- `BoardMovementComponent.cpp:472` caps lateral grip at `MassKg / DeltaTime`; at 60 fps the cap
  (5100 kg/s) is below full-edge-plus-tail grip (6250 kg/s), at 120 fps it is not.
- `:488` one explicit Euler step per frame; `:557-558` carve yaw step is `rate * dt` against a
  kinematic target, which is fine, but the velocity that follows it is not.
- `:399-414` the buoyancy spring is `3000 / 85 = 35 /s^2` explicit; it is stable at 60 fps
  (`omega dt = 0.1`) and goes unstable for a frame over about 0.34 s, where only the 20 cm clamp
  saves it (`:685-690`).
- `KiteComponent.cpp:852` the kite's sub-step is `dt / ceil(240 dt)`: 1/240 at 60 fps, 1/300 at
  100 fps, 1/250 at 125 fps. Not a fixed step.
- No test runs the same scenario at two step sizes. All fixtures use 1/60 or 1/30
  (`RideLoopTests.cpp:27`, `BoardMovementTests.cpp:89`).

Symptom: the same input rides differently at 30, 60 and 144 fps; replay and ghosts are
impossible; tuning done at one frame rate drifts at another.

### P2. Tick order between pawn, kite and board is unspecified and differs from the tests (structural)

- No `AddTickPrerequisiteComponent` ties the board to the kite or the pawn to either
  (`BoardMovementComponent.cpp:90-97`, `KiteRiderPawn.cpp:186-223`); the kite only depends on the
  wind (`KiteComponent.cpp:131`).
- The pawn hands over `Kite->GetLineForce()` at `KiteRiderPawn.cpp:431`. Whatever the order, the
  board integrates a force computed from the rider state of the previous frame.
- If the board ticks before the kite, the kite sees the new rider position and velocity
  (`KiteComponent.cpp:461-469`); if after, the old. The engine does not promise either.
- Tests step `Kite -> Pawn -> Board` (`RideLoopTests.cpp:71-73`), which no game frame does.

Symptom: a one-frame lag in the line force on the rider, visible as a softer pendulum in the
air and as differences between what the tests measure and what the game does; the order can
change with component registration order.

### P3. Wind is not a deterministic world field and has no gust fronts (structural, behaviour)

- Stateless Perlin of world time (`WindComponent.cpp:53,63-68`); no seed; no time parameter.
- No advection: gusts fade in place instead of travelling downwind, so nothing can be seen
  coming on the water.
- Shear clamps at `ShearHeightCm` (`:78`): 24 m lines see 10 m wind; the rider at Z = 0 sees 70%.
  Real profiles over water give about 8% more at 24 m than at 10 m and about 78% at 1 m.
- Three different "wind at the rider": pawn Z (`KiteComponent.cpp:362,368`,
  `KiteRiderPawn.cpp:732`) against +10 m (`KiteSurfHUD.cpp:267`).
- `GustNoiseGain` after the clamp (`:65`) breaks the `1 +- GustStrength` bound the test asserts
  only by luck of Perlin's range.
- Per-pawn component; mean direction hardcoded to +X by `KiteSurfGameMode.cpp:33` and
  `KiteSurf.cpp:82`.

Symptom: gusts feel random rather than readable; the kite is under-powered aloft; the HUD, the
kite and the audio disagree about the wind; no ride can be replayed.

### P4. Edging is viscous drag plus a thrust heuristic, not a force balance (behaviour, tuning)

- `BoardMovementComponent.cpp:465-482`: lateral force proportional to lateral speed, forward
  drive manufactured from it.
- Heel is cosmetic (`:540-542`); the carve input both turns the board and sets grip; weight
  shift scales grip by `2.5^(-w)` (`:470`).
- Upwind angle comes out of `EdgeDriveEfficiency = 0.35` and the grip coefficients, none of which
  map to a measurable quantity. The 15 s upwind test gains 2.1 m/s against 7.7 m/s of wind on a
  120 deg heading, which is plausible, but there is no edge angle to compare with the
  `tan(phi) = T_h / (m g - T_v)` balance in the research.
- A loaded edge cannot be held without turning: holding carve turns the board at 60 deg/s
  (`:552-559`). Weight shift is the only way to add grip without yaw.

Symptom: hard to make the board feel like it bites; no lever for "more edge, same heading"; the
numbers have no units to reason about.

### P5. Jumps are too short for their height; lift-off, pop and landing are thresholds and impulses (behaviour)

- Measured: best timed jump 14.8 m in 4.1 s (loop kite), 17.4 m in 5.0 s (boost), from
  `KiteSurf.Jump.TimedReleaseBeatsPop` and `KiteSurf.Gear.ChangesBehaviour`. Effective gravity
  `8h / t^2` = 7.0 and 5.6 m/s^2 against 1.5 to 3.5 m/s^2 for real jumps. The climb is right, the
  float is missing: the kite carries too little of the rider on the way down.
- Lift-off is a step function of kite vertical force against `1.5 .. 4.5` weights (`:388-397`);
  pop adds `0.22 s * F_kite,z` as an impulse (`:206`); landing zeroes `Vz` in one frame (`:650`)
  and reports `|Vz| / g` as a g-load (`:659`); a clean landing drops 20% of speed instantly
  (`:648-649`).
- Lift-off logs show the kite reaching 3.8 to 3.9 kN vertical on an 85 kg rider (4.7 g) before
  the edge lets go (test log `Board lifted off by the kite: upward force 3915 N`), above the
  2.5 to 4 body weights the research expects at take-off.

Symptom: jumps end abruptly, hang time reads short, landings are either free or a crash, and the
landing number on the HUD has no physical meaning.

### P6. Water contact is a clamp, and the vertical axis is mostly kinematic (behaviour)

- `:685-690` hard clamp to 20 cm with `Vz = 0`; `:348-350` float depth lerp; `:411` buoyancy
  clamped to 1500 N (1.8 body weights) so a hard landing cannot be decelerated by water.
- One sample point (`:341`); orbital velocity dropped (`KiteWaterSurface.cpp:28-33`).
- The kite's crash height is world `Z = 60 cm` (`KiteComponent.cpp:865`), not the water surface.

Symptom: the board sticks to the surface over chop instead of bouncing, cannot be slammed, and
landings do not feel the water.

### P7. Kite aerodynamics are self-consistent but uncalibrated in places (behaviour)

- `Cl(0) = 0` and `Cl` symmetric in alpha (`KiteComponent.cpp:557`); a tube kite has
  `Cl ~ 0.2` at zero and stalls near 20 to 25 deg. The trim range (`-22 .. +2`) compensates.
- No steering dead time; depower does not slow the turn (`:752` uses airspeed only); steering
  drag `0.06 steer^2` (`:689`) against a measured `Cd * (1 + 0.6 |steer|)`.
- Gravity turn term about a quarter of the measured value (`:756`, `GravityTurnGain 0.15`).
- `MaxLineTensionN = 6000` (7.2 body weights) is the only line compliance.
- Relaunch teleports (`:527-535`).

Symptom: loops and sends respond slightly too cleanly; a depowered kite turns as fast as a
powered one; a slow kite does not drop its nose as a real one does.

### P8. The assist controller is inside the physics and runs on the frame step (tuning)

- `ComputeSteering` (`:576-657`) is called once per frame with the frame dt (`:844`), feeds the
  sub-steps a constant command, and uses frame-rate-sensitive state (`CentredBarSeconds`).
- Its park hold measures clock position in the apparent-wind window, which rotates as the rider
  accelerates, so a parked kite drifts with rider speed changes.

Symptom: controller feel and physics tuning cannot be changed independently; a feel tweak can
change test results.

### P9. Mixed units and repeated constants (tuning)

See the table in 4.5. The knots constant lives in 20 files; the unit of `MaxBoardSpeed` is
guessed from its size; gravity is defined twice; force conversions are done by hand.

Symptom: a change in one place silently disagrees with another; tuning values cannot be read
without the code.

### P10. Hitches are not bounded (structural)

- `MaxFlightStepsPerUpdate = 24` (`KiteComponent.cpp:18`) lets the kite step grow past 1/240 s
  for frames over 100 ms; the board takes the whole frame in one step; no `MaxDeltaTime`.

Symptom: a shader-compile hitch or a window drag can throw the kite or the board.

### P11. Debug visualisation is kite-only and not switchable at run time (tuning)

- `bDrawDebug` draws the line, the heading and a text block (`:886-894`). Nothing for wind at
  the rider, apparent wind, lift, drag, side force, board grip or gust intensity. No console
  variable.

### P12. Verification gaps (tuning)

- No step-rate independence test; no golden trajectories; the placeholder test
  `KiteRiderPawnTests.cpp:27` asserts `true`; `scripts/run-tests.sh:16-17` only warns when the
  report is missing.

### P13. Smaller items

- `KiteComponent.cpp:468` reads `GetOwner()->GetVelocity()`, which is the board's `Velocity`
  member; its age depends on P2.
- `WindComponent.cpp:20-40` and `KiteSurfGameMode.cpp:33` both set `BaseWind` from the game
  instance; the second wins and discards the first's direction.
- `BoardMovementComponent.cpp:421` drag regime switch at exactly 400 cm/s with no hysteresis.
- `KiteComponent.cpp:403-404` mass scales with area and added mass with area^1.5, undocumented.
- `BoardMovementComponent.cpp:509-523` air spin and auto-align are rates in deg/s with no
  angular momentum, which is fine for a game but means a spin cannot be "loaded".

## 6. What is right and should be kept

- The kite model's structure: point mass on a line sphere, SI inside, apparent wind with the
  kite's own velocity, analytic tension, taut and slack states, stall and luff, sub-stepping,
  semi-implicit Euler with projection. This is what the research asks for and it works; it needs
  a fixed step, calibration and a few extra terms, not a rewrite.
- The clock and depth window API (`SetWindowPosition`, `GetClockDeg`, `GetWindowDepthDeg`) and the
  assist's intent (bar over = travel, centred = hold, towards own side = loop).
- `IKiteWaterSurface` with flat, sine and Water-plugin implementations, waves included.
- The board state machine (displacement, planing, airborne, landing) and the gear trait tables.
- The test fixtures that step the coupled pawn and the numbers they log.

## 7. Baseline measurements

From the `-nullrhi` run at `3f3c389`, 60 Hz fixtures, no gusts:

| Scenario | Measured | Real-world ballpark (see `research.md`) |
| --- | --- | --- |
| 12 m^2 kite, 15 kn, no input, 30 s | 14.3 kn, 467 N, kite az 31 el 23 | Riding tension 0.5 to 1.0 body weight (420 to 830 N) |
| 9 m^2, 20 kn, bar out / half / in | 8.4 kn 152 N, 13.4 kn 341 N, 21.6 kn 1101 N | Depowered about 0.5 BW, powered 1 to 1.3 BW |
| 12 m^2, 12 / 20 / 28 kn | 11.3 / 19.2 / 18.2 kn (28 kn case sheeted out) | 15 to 25 kn riding, up to 35 |
| 7 m^2 vs 14 m^2 parked, 15 kn | 160 N vs 321 N | Linear in area |
| Upwind, 120 deg heading, 15 kn | 5.5 m/s forward, 0.75 m/s leeway, 2.1 m/s upwind | 15 to 25 deg above a beam reach |
| Best timed jump, 30 kn, 6 m^2 | 14.8 m, 4.1 s; boost kite 17.4 m, 5.0 s | 12 to 20 m; 5 to 8 s; `8h/t^2` 1.5 to 3.5 m/s^2 (here 7.0 and 5.6) |
| Pop with kite parked | 1.5 m | Under 1 m plus kite lift |
| Loop vs boost, 3 s of bar | 499 deg vs 382 deg | Loop 1.5 to 2.5 s per 360 |
| Lift-off vertical force logged | 1260 to 3915 N on 833 N of weight | Take-off 2.5 to 4 BW |

The kite's numbers are in range. The board's speed is in range. The jump airtime is the
clearest miss.
