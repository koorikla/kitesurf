# Physics plan

Phase 3 of the physics rework. Changes ordered by impact against risk, each with what it fixes
from `review.md` (P-numbers), what it costs, and how it is verified. Items marked **now** are
low-risk and high-impact and are implemented in this pass; items marked **deferred** change
riding feel or controls enough that they need a decision, and are documented with a starting
design instead.

Ground rules carried through every item: C++ for the physics core; every tuning value a
`UPROPERTY` with its unit in the name or tooltip; SI inside the simulation and one conversion
at the boundary; existing input actions, console commands, HUD getters and test entry points
keep working, and any interface that has to change is listed in section 3.

## 1. Changes

### 1. One set of units and constants (now)

- **Fixes** P9 (25 copies of `51.44`, two gravities, hand-written newton conversions, a getter
  that guesses its unit).
- **Change** Add `KiteSurfUnits.h` with `KiteUnits::CmPerKnot`, `CmPerM`, `UnrealForcePerN`,
  `GravityMS2`, `AirDensityKgM3`, `WaterDensityKgM3` and helper conversions; use them in the
  runtime module. `UKiteWindMath::KnotsToCmPerSec` stays as the Blueprint-facing wrapper.
  `GetMaxBoardSpeedCmS` stops guessing: `MaxBoardSpeed` is renamed `MaxBoardSpeedCmS`.
- **Cost** S. **Risk** low: mechanical; the tests are the safety net.
- **Verify** Build; the 73 tests unchanged; `grep 51.44 Source/KiteSurf --include=*.cpp
  --include=*.h | grep -v Tests` returns one definition.

### 2. Fixed-step simulation driven by the pawn, in one order, with render interpolation (now)

- **Fixes** P1 (frame-step board, frame-dependent sub-step), P2 (unpinned tick order and the
  one-frame force lag), P10 (unbounded hitches).
- **Change** `AKiteRiderPawn::Tick` owns a time accumulator and a fixed `SimStepSeconds`
  (1/240 s). Each step, in this order: wind sampled at sim time, kite step with the rider's
  current state, line force handed to the board, board step. The kite and board components stop
  ticking on their own when a pawn drives them (`bCanEverTick` false), but `UpdateKite(dt)`
  and `TickComponent(dt)` remain callable and sub-step internally, so component-only tests keep
  working. Frame time is clamped (`MaxFrameSeconds` 0.1) and steps per frame capped
  (`MaxStepsPerFrame`), so a hitch slows the simulation instead of exploding it. After the loop
  the pawn's root is placed at the transform interpolated between the previous and current sim
  state by the accumulator's remainder, and restored to the sim state before the next step; the
  kite's visuals interpolate the same way. Other systems (camera, HUD, wake, spot) read the
  interpolated actor transform, which is what they should see.
- **Cost** M. **Risk** medium-low: it touches the core loop, but the step maths does not
  change; the risk is in the plumbing, which the full suite exercises.
- **Verify** New test `KiteSurf.Physics.StepRateIndependent`: the same scripted ride (steady
  wind, then a send and a pop) at frame rates of 30, 60 and 120 Hz, with the same fixed step,
  gives speed, position and apex within tolerance (position within 10 cm after 20 s, apex
  within 2%). `KiteSurf.Physics.HitchIsBounded`: a 1 s frame does not produce NaN or a kite
  past line length. The 73 existing tests pass with the fixture stepping the pawn once per
  frame.

### 3. Frame-rate-independent drag and a readable buoyancy spring (now)

- **Fixes** P1 (the `m / dt` grip cap, explicit viscous terms), part of P9.
- **Change** Lateral grip and linear drags integrate exactly (`v *= exp(-c dt / m)`), quadratic
  drags implicitly (`v /= 1 + k |v| dt / m`), both inside the fixed step; the `m / dt` cap goes.
  Buoyancy is parameterised by `BuoyancyNaturalFrequencyHz` and `BuoyancyDampingRatio` instead
  of two bare numbers (defaults chosen to reproduce today's 3000 and 800: 0.94 Hz, 0.79).
  Coefficient names gain units: `PlaningDragKgPerS`, `PlaningQuadraticDragKgPerCm`,
  `BaseGripKgPerS`, `EdgeGripKgPerS`, `PlaningLiftKgPerS`, `PopImpulseKgCmPerS`,
  `AutoHeelFullLoadN`, `LowSpeedPivotMinForceN`.
- **Cost** S. **Risk** low: the settled behaviour is the same; the transient no longer depends
  on the step. Full edge with weight back gains the grip the cap was removing at 60 fps
  (6250 against 5100 kg/s), which the ride tests will show.
- **Verify** Existing board and ride tests; the step-rate test from item 2 covers the board.

### 4. Wind as a deterministic, sampleable, travelling field with a real profile (now)

- **Fixes** P3 (no seed, no time argument, no advection, shear capped at 10 m, three
  different "wind at the rider", gain after the clamp), P13 (duplicate base-wind setup).
- **Change** `UWindComponent` keeps its parameters and `GetWindAt(pos)` but the field becomes a
  pure function `GetWindAt(pos, timeSeconds)` of a `Seed`: mean wind times a power-law profile
  (`ShearExponent` 0.11 about `ReferenceHeightCm` 1000, floor at `MinSampleHeightCm` 100 so the
  surface is never sampled at zero), times a gust factor from two octaves of seeded gradient
  noise advected downwind at the mean speed (`GustCellLengthCm` along, half that across,
  `GustEvolveSeconds` slow axis), plus a direction wander from a third noise with
  `DirectionDriftDeg` as its standard deviation times about 2. `GustStrength` is defined as the
  peak-to-mean factor the field reaches (the measured 1.23 to 1.38 at sea), and the noise is
  normalised so the field actually reaches it. The pawn samples the field at sim time, the
  kite at its own height, the rider's apparent wind and window axis at `RiderWindHeightCm`
  (150), the HUD at the reference height. The game mode and the component's `BeginPlay` both
  reading the game instance is reduced to one path. `GetGustFactorAt` keeps its meaning.
- **Cost** M. **Risk** low-medium: every consumer changes its number a little (the kite gets
  about 10% more wind aloft, the rider about 20% less at chest height than at 10 m); the ride
  tests will move and are re-based where the physics says they should.
- **Verify** `KiteSurf.Wind.*` updated: profile values at 1.5, 10 and 25 m; mean within 2% of
  base over a long sample; gust factor reaches `GustStrength` and stays within it; a gust seen
  at a point upwind arrives at the rider `distance / U` later (`KiteSurf.Wind.GustsTravel`);
  same seed gives the same field, a different seed a different one. `KiteSurf.Physics.GustHitsMidRide`
  and `KiteSurf.Physics.LullDropsKite` from the verification list.

### 5. Debug visualisation and telemetry behind a console variable (now)

- **Fixes** P11.
- **Change** `kite.Physics.Debug 1` draws, at the kite: true wind, apparent wind, lift, drag,
  side force, line tension, heading, and a text block with airspeed, angle of attack, Cl, Cd,
  tension, taut or slack, step count; at the rider: true wind at chest height, apparent wind,
  the line force, the board's grip force, drive and drag, edge angle, leeway, board state; and
  the gust factor as a bar. `kite.Physics.Debug 2` adds a CSV line per step to the log
  (`Saved/Telemetry` is left for later). Implemented in the pawn with `DrawDebug*`, compiled
  out in Shipping.
- **Cost** S. **Risk** none to physics.
- **Verify** Compiles in Development; `-game` run not required for the physics claims but the
  command is documented for the user to try.

### 6. Scenario tests and real-world sanity numbers (now)

- **Fixes** P12.
- **Change** `Source/KiteSurf/Private/Tests/PhysicsScenarioTests.cpp` with
  `KiteSurf.Physics.SteadyRideAcross` (12 m^2, 75 kg, 15 to 20 kn: board speed, tension and
  kite position in range), `KiteSurf.Physics.UpwindAtEdgeAngle` (held edge, course gain against
  the wind), `KiteSurf.Physics.ParkedAtZenith` (9 m^2 at 30 kn: tension 0.9 to 1.0 kN sheeted
  in, under 0.45 kN out), `KiteSurf.Physics.GustHitsMidRide`, `KiteSurf.Physics.LullDropsKite`,
  `KiteSurf.Physics.StepRateIndependent`, `KiteSurf.Physics.HitchIsBounded`, plus invariants
  (no NaN, line length never above its limit, tension never negative). The placeholder
  assertion in `KiteRiderPawnTests.cpp:27` becomes a real one.
- **Cost** M. **Risk** none to physics.
- **Verify** The suite's `Total` rises by the number added; `Failed=0`; measured numbers are
  copied into the CHANGELOG table against the real-world ballpark.

### 7. Kite steering and aerodynamics: the missing terms as tunables (now, calibrated carefully)

- **Fixes** P7.
- **Change** Add `SteeringDeadTimeSeconds` (input delay line, research 0.2 powered to 0.6
  depowered; implemented as powered value plus `DepoweredDeadTimeExtraSeconds` scaled by
  `1 - Sheet`), `DepoweredTurnRateFactor` (turn gain at bar out as a fraction of bar in,
  research 0.4 to 0.5), the gravity turn term re-expressed as `GravityTurnRadMPerS2`
  (Fechner's `c_2`, 6.28 for 10 m^2), steering drag as `SteeringDragFactor` in
  `Cd * (1 + f |steer|)` (research 0.6), and a `ZeroLiftAngleDeg` camber offset so `Cl(0)` is
  not zero. The lift and drag curve keeps its shape. Defaults start at the research values;
  any that break a ride, loop or jump test is set back to today's behaviour and its research
  value recorded in the CHANGELOG as the value to tune towards.
- **Cost** M. **Risk** medium: these change how loops and sends feel. Mitigated by one commit
  per term, each run against the full suite.
- **Verify** `KiteSurf.Kite.*` and `KiteSurf.Jump.*`; a new `KiteSurf.Kite.DepowerSlowsTheTurn`
  (turn rate at bar out is 35 to 55% of bar in) and `KiteSurf.Kite.LoopRadiusIsSpeedIndependent`
  (full-bar loop radius within 15% at two airspeeds).

### 8. Rider body drag in the air (now)

- **Fixes** part of P5.
- **Change** `RiderDragAreaM2` (0.7) applied to the point mass against the apparent wind while
  airborne only.
- **Cost** S. **Risk** low: 40 to 270 N, additive, airborne only.
- **Verify** `KiteSurf.Jump.TimedReleaseBeatsPop` still clears its thresholds.

### 9. Jump float diagnosis (now: measure; tune only where the model says so)

- **Fixes** the airtime half of P5.
- **Change** With item 5's telemetry, log a timed jump and read the vertical line force, angle
  of attack and kite elevation through the descent. The research says the kite should carry
  65 to 85% of the rider's weight on the way down; the baseline's `8 h / t^2` of 7 m/s^2 says it
  carries about 30%. The likely causes are a stalled canopy as the sink speed raises the angle
  of attack past 18 deg sheeted in, or the assist letting the kite drift low. If it is the
  stall angle or the camber offset, item 7 fixes it; if it is the assist, the fix is a tuning
  value on the park-hold. No scripted lift is added.
- **Cost** S to M. **Risk** medium if a tuning value moves: the jump tests bound it.
- **Verify** `KiteSurf.Physics.HangTime`: a 10 to 20 m timed jump at 30 kn has
  `8 h / t^2` between 1.5 and 5 m/s^2 (the real range is 1.5 to 3.5; the upper bound is loose
  until the edge and landing models are redone). If the baseline cannot meet it without a feel
  change, the test is written with the measured value and the gap goes in the CHANGELOG.

### 10. Edging from a force balance, with the edge separated from heading (deferred)

- **Fixes** P4 and the mechanism half of P5.
- **Design** Replace viscous grip and the drive heuristic with: an edge angle `phi` driven by
  the carve input and weight shift (and later a dedicated edge input), a lateral lifting area
  that grows with heel (fins plus immersed rail), side force from leeway angle, and the water's
  normal force tilted by `phi` so the horizontal pull is carried directly (`N sin(phi) = T_h`).
  Lift-off becomes a force balance: the board leaves the water when the normal force would go
  negative. Upwind ability then follows from the two drag angles.
- **Why deferred** It retunes every ride test and changes what the carve stick does; the
  research backlog sizes it L. It also wants a decision on controls: a separate edge input
  (left trigger "load edge") exists in the design doc but not in the input assets.
- **Verification when done** `KiteSurf.Physics.UpwindAtEdgeAngle` with the edge angle as the
  input; beam reach at 25 kn holds 18 to 25 kn and points 15 to 25 deg upwind; a flat board
  slides downwind.

### 11. Compliant lines at the real stiffness (deferred)

- **Fixes** the "infinitely stiff" note in P7.
- **Design** XPBD compliance `1 / LineStiffnessNPerM` (8 to 12 kN/m) on the existing distance
  constraint, with `LineDampingRatio`; tension read from the multiplier.
- **Why deferred** A small behaviour change for a non-trivial stability review; the current
  projection is already stable and the cap on tension can stay as a safety.

### 12. Multi-point water sampling, wave orbital velocity, landing from absorb distance (deferred)

- **Fixes** P6 and the landing half of P5.
- **Design** Five samples under the board, a fitted plane, relative vertical velocity for
  slam and pop, the water's normal force replacing the 20 cm clamp, landing g from
  `1 + v_sink^2 / (2 g s_absorb)` with `LandingAbsorbDistanceCm`.
- **Why deferred** Changes how landings feel and when they crash; needs the force-balance edge
  (item 10) to be consistent.

### 13. World wind subsystem and water darkening (deferred)

- **Design** Move the field from the pawn's component to a `UWorldSubsystem`, with the
  component as a proxy; feed a Material Parameter Collection for gust bands and Niagara for
  streaks. Item 4 makes the field a pure function so this is a move, not a rewrite.
- **Why deferred** Visual and multi-pawn work, outside the physics goal.

## 2. Order of work and what each step is checked against

| # | Item | Fixes | Size | Risk | Gate |
| --- | --- | --- | --- | --- | --- |
| 1 | Units | P9 | S | low | 73 tests unchanged |
| 2 | Fixed step, order, interpolation, hitch cap | P1 P2 P10 | M | medium-low | 73 tests + step-rate + hitch tests |
| 3 | Exact drag, named buoyancy | P1 P9 | S | low | 73 tests |
| 4 | Wind field | P3 P13 | M | low-medium | wind tests rewritten, gust tests |
| 5 | Debug cvar | P11 | S | none | compiles |
| 6 | Scenario tests, sanity table | P12 | M | none | Total rises, Failed=0 |
| 7 | Kite terms as tunables | P7 | M | medium | kite, jump tests, two new |
| 8 | Rider body drag | P5 | S | low | jump tests |
| 9 | Jump float diagnosis | P5 | S-M | medium | hang-time test or documented gap |
| 10 | Force-balance edging | P4 P5 | L | high | deferred |
| 11 | Compliant lines | P7 | M | medium | deferred |
| 12 | Water contact and landing | P5 P6 | M | medium | deferred |
| 13 | Wind subsystem and visuals | P3 | M | low | deferred |

## 3. Interfaces that change

- `UWindComponent::GetWindAt(const FVector&)` stays; `GetWindAt(const FVector&, float TimeSeconds)`
  is added and is what the simulation calls. `TimeOverride` stays for tests. `ShearHeightCm`
  is kept as the reference height's name for compatibility and documented as such.
- `UKiteComponent` and `UBoardMovementComponent` no longer tick on their own when their owner
  is an `AKiteRiderPawn`; the pawn steps them. Calling `UpdateKite(dt)` or `TickComponent(dt)`
  directly still advances them by `dt`.
- Test fixtures that stepped kite, pawn and board separately now call `Pawn->Tick(dt)` once per
  frame (`RideLoopTests.cpp`, `MovementTests.cpp`, `BoardMovementTests.cpp` where the pawn
  is involved).
- `UBoardMovementComponent::MaxBoardSpeed` becomes `MaxBoardSpeedCmS`; tuning properties gain
  unit suffixes (listed in item 3). Blueprint defaults are not overridden by the project's
  assets (`make_input_assets.py` sets only input and camera), so the renames are safe.
- Console: `kite.Physics.Debug` added. `kitesurf.Wind` unchanged.

## 4. What this pass does not do

Items 10 to 13. The riding feel on the water (viscous grip, drive heuristic, kinematic heel)
is left as it is, with units and a fixed step under it, until the edge model decision is made.
Line stretch, multi-point water contact and physically derived landing loads wait for that.
