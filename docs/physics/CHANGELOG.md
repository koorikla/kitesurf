# Physics rework: changelog

What the `physics/rework` branch changed, commit by commit, what to tune next, and what is still
missing. Read with `review.md` (the code before), `research.md` (what the physics says) and
`plan.md` (the items referred to by number). Every number below comes from the `-nullrhi`
automation tests named next to it (`scripts/run-tests.sh -nullrhi`); none of it was seen in a
`-game` run.

## What changed

1. `3f3c389` **feat(bar): the bar is a real throttle and moves fast; 9 m kite in 20 kn by
   default.** The trim range goes from -13..-1 to -22..+2 deg and `SheetRatePerSec` from 0.8 to
   2.5 (the whole throw in 0.4 s). 9 m in 20 kn: bar out 8.4 kn and 152 N, half 13.4 kn and
   341 N, in 21.6 kn and 1101 N (`KiteSurf.Ride.BarIsTheThrottle`). 73 tests.
2. `9f12249` **docs(physics): review.** Component map, data flow and ranked problems P1 to P13.
   Baseline: best timed jump 14.8 m in 4.1 s at 30 kn, effective gravity `8h/t^2` 7.0 m/s^2.
3. `247d73a` **docs(physics): research.** Turn-rate law, lift and drag, wind profile and gusts,
   planing, tethers and fixed-step methods, each number tagged sourced, typical or estimate.
4. `02583ec` **docs(physics): plan.** Thirteen items ordered by impact against risk; 1 to 9 in
   this pass, 10 to 13 deferred with designs.
5. `02a4e38` **refactor(units)** (plan item 1). `KiteSurfUnits.h` holds every unit constant and
   conversion; 25 copies of 51.44 and two values of g are gone; `MaxBoardSpeed` becomes
   `MaxBoardSpeedCmS`. Results unchanged.
6. `ac4d987` **feat(sim): fixed step** (item 2). The pawn steps kite, line force and board at
   `SimStepSeconds` (1/240 s) in that order, clamps frames to `MaxFrameSeconds` (0.1 s), caps
   steps at `MaxSimStepsPerFrame` (32) and draws between the last two steps. Results unchanged
   to the decimal.
7. `28b6bc1` **feat(board): exact drag and grip, tunables with units** (item 3). Drag and grip
   are closed-form decays in the step (no `m / dt` cap); buoyancy is
   `BuoyancyNaturalFrequencyHz` 0.945 and `BuoyancyDampingRatio` 0.79. Timed jump 14.8 -> 15.0 m.
8. `d101a67` **feat(kite): bar centred drifts to 12.** `ZenithDriftGain` 1.0 deg per deg up to
   `ZenithDriftMaxHeadingDeg` 20; the old hold is `bParkHoldAssist`. From clock 65 within 10 deg
   of 12 in 11.4 s, standing in 15 kn. 74 tests.
9. `249003e` **feat(wind): seeded, travelling field** (item 4). Power law 0.11 about 10 m, gusts
   advected at the mean speed and bounded by `1 +- GustStrength`, direction drift 5 deg, the
   rider's wind at `RiderWindHeightCm` (1.5 m). The kite sees about 10% more wind aloft: timed
   jump 15.0 -> 17.6 m in 4.4 s; gear jumps loop 15.8 m, boost 18.8 m. 76 tests.
10. `ef1a8ba` **feat(debug): `kite.Physics.Debug`** (item 5). Level 1 draws the forces, level 2
    logs a `kitecsv` line per step. Riding at 13.5 kn: lift 427 N, drag 83 N, tension 423 N, Cl
    0.64, Cd 0.13. 77 tests.
11. `ed2006c` **test(physics): scenario tests** (item 6). `PhysicsScenarioTests.cpp` with the
    invariants (no NaN, never past line length, tension never negative) in every scenario:
    12 m^2 at 15 / 20 kn 14.3 kn 468 N / 19.2 kn 806 N; upwind 1.76 m/s made good, leeway 9.1
    deg; 9 m^2 at the zenith in 30 kn 1656 N sheeted in (a known gap, below), 359 N out; a gust
    to 22 kn doubles the pull; a lull to 3 kn drops the kite at once; 30, 60 and 120 fps give
    identical rides; a 1 s hitch runs 23 steps. The placeholder pawn test asserts real clamps.
    84 tests.
12. `0b27abc` **feat(kite): steering terms** (item 7). Dead time 0.15 s plus 0.3 s with the bar
    out, depowered turn rate 0.45, gravity turn `GravityTurnRadMPerS2` 2.4 (research 6.28),
    steering drag `Cd * (1 + 0.6 |steer|)` on attached flow, `ZeroLiftAngleDeg` 0 (research -3).
    Depowered the kite turns 36% as far (steering gain 48%); full-bar loop radius 6.3 m at 15 kn
    and 6.5 m at 25 kn. Timed jump 17.6 -> 14.6 m, 3 s of loop 553 -> 355 deg, mostly from the
    steering drag. 86 tests.
13. `f62ccbe` **feat(board): air drag on the rider in the air** (item 8). `RiderDragAreaM2` 0.7:
    96.5 N at 15 m/s. Timed jump 14.6 -> 13.1 m in 3.7 s; gear jumps loop 11.7 m, boost 12.9 m.
    87 tests.
14. `2f9d750` **test(physics): hang-time diagnosis** (item 9). `KiteSurf.Physics.HangTime` traces
    the timed jump at 20 Hz: 13.1 m in 3.67 s, `8h/t^2` 7.77 m/s^2. The kite is low and to the
    side of the airborne rider on the way down and lifts 10% of their weight; no one or two
    tunables fix it. 88 tests.
15. This commit: **docs(physics)**, this changelog and the tunable tables, fixed step, wind and
    bar-centred behaviour in `docs/movement.md`, `docs/jumping.md`, `docs/ARCHITECTURE.md`, and
    the jump heights in `README.md`.

### Where the numbers stand

| Scenario (test) | Measured | Real-world ballpark (`research.md`) |
| --- | --- | --- |
| 12 m^2, 81 kg, 15 kn, 30 s (`Physics.SteadyRideAcross`) | 14.7 kn, 508 N (0.64 BW), kite 22.5 deg up | 12 to 25 kn; riding pull 0.5 to 1.0 BW |
| same, 20 kn | 19.5 kn, 844 N (1.06 BW), kite 23.5 deg up | as above |
| 25 deg above a beam reach, 15 kn (`Physics.UpwindAtEdgeAngle`) | 1.72 m/s made good, leeway 9.6 deg | 2 to 3 m/s; a few deg of leeway |
| 9 m^2 at the zenith, 30 kn (`Physics.ParkedAtZenith`) | in 1656 N, out 359 N | in 0.9 to 1.0 kN, out about 380 N |
| 15 -> 22 kn gust (`Physics.GustHitsMidRide`) | 506 -> 978 N, 14.6 -> 21.1 kn | pull with v^2: x2.15 |
| Full-bar loop, standing (`Kite.LoopRadiusIsSpeedIndependent`) | 6.3 m in 2.28 s at 15 kn, 6.5 m in 1.43 s at 25 kn | radius independent of speed; 1.5 to 2.5 s a loop |
| Depowered turn (`Kite.DepowerSlowsTheTurn`) | 36% of the degrees, 48% of the steering gain | gain 0.15 against 0.35 rad/m (43%) |
| 9 m^2, 20 kn, bar out / half / in (`Ride.BarIsTheThrottle`) | 9.0 kn 172 N / 13.8 kn 367 N / 21.7 kn 1120 N | depowered about 0.5 BW, powered 1 to 1.3 BW |
| Timed jump, 30 kn, 6 m^2 (`Jump.TimedReleaseBeatsPop`, `Physics.HangTime`) | 13.1 m in 3.67 s (best release 14.9 m) | 10 to 20 m; 5 to 7 s for 15 m |
| Effective gravity `8h/t^2` | 7.8 m/s^2 | 1.5 to 3.5 m/s^2 |
| Pop with the kite parked | 1.5 m | under 1 m plus kite lift |
| Same ride at 30, 60, 120 fps (`Physics.StepRateIndependent`) | identical | identical |

## What to tune

The tunables that move the riding and jumping most, with today's default and the value to tune
towards. "Estimate" values in `research.md` are starting points, not requirements.

| Tunable | Default | Research value to tune towards | Notes |
| --- | --- | --- | --- |
| `UKiteComponent::GravityTurnRadMPerS2` | 2.4 rad m/s^2 (12 m^2) | 6.28 (Fechner c_2, 10 m^2) | Above 2.4 the park-hold assist (`ParkHoldGain` 2) settles more than 4 deg off its clock; raise the gain with it. |
| `UKiteComponent::ZeroLiftAngleDeg` | 0 deg | -3 deg | At today's trim -3 adds enough power that a small board planes in 12 kn and upwind leeway passes 10 deg; lower both trims by 2 to 3 deg with it. |
| `UKiteComponent::SteeringDragFactor` | 0.6 | 0.6 (Fechner) | At research value. Costs about 3 m of the 30 kn jump (17.9 m at 0). |
| `UKiteComponent::SteeringDeadTimeSeconds` / `DepoweredDeadTimeExtraSeconds` | 0.15 s / 0.3 s | 0.2 s powered to 0.6 s depowered (Elfert 2024) | Delays the rider's bar; the assist is not delayed. |
| `UKiteComponent::DepoweredTurnRateFactor` | 0.45 | 0.43 (0.15 / 0.35 rad/m) | |
| `UKiteComponent::TrimSheetedOutDeg` / `TrimSheetedInDeg` | -22 / +2 deg | a bar throw of 12 to 20 deg of angle of attack | 24 deg today; with camber the range moves down. |
| `UKiteComponent::MaxLiftCoefficient` / `StallAngleDeg` | 1.2 / 18 deg | 1.0 to 1.2 at 16 to 20 deg | |
| `UKiteComponent::MinTurnRadiusCm` | 420 cm at 12 m^2 | g_k 0.26 rad/m for 10 m^2 (3.8 m), 0.15 to 0.35 | Scales with sqrt(area). |
| `UKiteComponent::MaxLineTensionN` | 6000 N | line stretch of 1 to 1.5% (item 11) | Stands in for line compliance. |
| `UKiteComponent::ZenithDriftGain` / `ZenithDriftMaxHeadingDeg` | 1.0 / 20 deg | a kite with the bar neutral reaches 12 in about 10 s | Feel, not physics. |
| `UBoardMovementComponent::RiderDragAreaM2` | 0.7 m^2 | 0.5 to 1.0 m^2 | |
| `UBoardMovementComponent::EdgeReleaseSeconds` | 0.22 s | none: a pseudo-impulse | 5.7 of the 10 m/s take-off speed at 30 kn; see the hang-time gap. |
| `UBoardMovementComponent::PopImpulseKgCmPerS` | 21000 kg cm/s (2.5 m/s on 85 kg) | 1 to 2 m/s from the legs | |
| `UBoardMovementComponent::LiftoffWeightFactor` / `EdgedLiftoffWeightBonus` | 1.5 / 3.0 body weights | take-off at 2.5 to 4 body weights | |
| `UBoardMovementComponent::PlaningDragKgPerS` / `PlaningQuadraticDragKgPerCm` | 8 kg/s / 0.03 kg/cm | `a + c v^2` with a about 60 N, c 0.5 to 1.0 N s^2/m^2 | Sets riding speed against the kite. |
| `UBoardMovementComponent::BaseGripKgPerS` / `EdgeGripKgPerS` / `EdgeDriveEfficiency` | 500 / 2000 kg/s / 0.35 | replaced by force-balance edging (item 10) | |
| `UWindComponent::ShearExponent` | 0.11 | 0.11 +- 0.03 | |
| `UWindComponent::GustStrength` | 0.3 | 3 s gust factor 1.23 at sea, 1.38 off-sea | |
| `UWindComponent::DirectionDriftDeg` | 5 deg | 4 to 6 deg | |
| `UWindComponent::GustCellLengthCm` / `GustEvolveSeconds` | 6000 cm / 180 s | 50 to 100 m; a minute or more | |
| `AKiteRiderPawn::SimStepSeconds` | 1/240 s | 1/240 s (1/120 would do) | |

## Known gaps

- **Hang time** (item 9). `8h/t^2` is 7.8 m/s^2 against 1.5 to 3.5. The take-off speed is mostly
  the edge-release impulse, the rider climbs level with the kite, and on the way down the kite is
  low and to the side, lifting about 10% of the rider's weight. The assist steers by the wind the
  rider feels, which in the air is dominated by the rider's own climb and fall. A code experiment
  (assist steering by the horizontal wind, no steering drag, a 28 deg stall) reached 5.8 with the
  kite back 56 deg over the rider for part of the descent. Needed: an airborne assist that flies
  the kite to the zenith of the rider's window and holds it there, and a send in which the kite
  keeps climbing ahead of the rider instead of a launch impulse. `KiteSurf.Physics.HangTime` pins
  today's value.
- **Zenith pull.** The forces use the kite's flat area; a 9 m^2 kite at the zenith in 30 kn pulls
  1.66 kN against the research's 0.9 to 1.0 kN. A projected-area factor (0.65 to 0.8) would fix
  it and take the 15 kn riding pull from 0.64 to about 0.46 body weights, so it waits for a
  calibration pass with the trims and the board drag. `KiteSurf.Physics.ParkedAtZenith` pins
  today's value.
- **Research values not yet at their defaults:** `GravityTurnRadMPerS2` (2.4, research 6.28) and
  `ZeroLiftAngleDeg` (0, research -3); see the table above.
- **Item 10, force-balance edging** (deferred): edge angle separate from heading, side force from
  leeway and heel, lift-off from the water's normal force.
- **Item 11, compliant lines** (deferred): XPBD compliance at 8 to 12 kN/m instead of the rigid
  projection and the `MaxLineTensionN` cap.
- **Item 12, water contact and landing** (deferred): five-point water sampling, wave orbital
  velocity, landing g from sink speed and absorb distance instead of the 20 cm clamp and the
  `|v_z| / g` figure.
- **Item 13, world wind subsystem and water darkening** (deferred): the field moves to a
  `UWorldSubsystem` and drives the gust bands on the water.
- **Hitches.** With the defaults the frame clamp (0.1 s) binds before `MaxSimStepsPerFrame`, and
  0.1 s in float holds a hair under 24 steps, so a long frame runs 23 and the 24th waits for the
  next frame. Harmless, noted for whoever tunes the clamp.
- **Nothing here was checked in a `-game` run**, including the debug drawing.
