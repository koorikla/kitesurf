# Physics rework: changelog

What the `physics/rework` branch (phase 1, merged as PR 45 at `13f8e13`) and the `physics/phase2`
branch changed, commit by commit, what to tune next, and what is still missing. Read with
`review.md` (the code before), `research.md` (what the physics says), `plan.md` (phase 1's items,
referred to by number) and `plan-2.md` (phase 2's). Every number below comes from the `-nullrhi`
automation tests named next to it (`scripts/run-tests.sh -nullrhi`) or from release sweeps run the
same way; none of it was seen in a `-game` run.

## Phase 1: what changed

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
15. `8f9a52f` **docs(physics)**, this changelog and the tunable tables, fixed step, wind and
    bar-centred behaviour in `docs/movement.md`, `docs/jumping.md`, `docs/ARCHITECTURE.md`, and
    the jump heights in `README.md`.
16. **merge: origin/main into physics/rework.** Main's #34 to #44 on top of the fixed step. The pop
    any time on the water and the loaded crouch (#41) live in the board: the load builds and lets
    go inside `StepBoard`, adds grip there and holds the board down like a full edge, and the pop
    keeps `PopImpulseKgCmPerS` and `EdgeReleaseSeconds`. The jointed rider (#43), motion bar and
    vibration (#37, #40), audio (#44) and gear preview (#42) run in the pawn's per-frame part and
    see the drawn transform. The wind streaks (#36) read the field at the water and the HUD's wind
    flag at `ReferenceHeightCm`. Loaded on the 9 m in 20 kn, slip 90 -> 39 cm/s and tension 585 ->
    677 N, take-off 4.6 m/s against 3.1 from a tap (`KiteSurf.Jump.LoadAndRelease`); every number
    above is unchanged. 104 tests.

## Phase 2: what changed

The `physics/phase2` branch, cut from `13f8e13` (phase 1 merged as PR 45), implementing
`plan-2.md`. The feel at `13f8e13` was judged fine by the user; every entry says what moved
against the commit before it, and the section after the list what moved against `13f8e13`.

17. `d2349d9` **docs(physics): plan for phase 2.** `plan-2.md`: projected area, the airborne kite,
    force-balance edging and water contact, with targets, tests and risks.
18. `e3e112b` **feat(kite): projected area, camber and trim** (plan-2 item 1). The air acts on
    `AreaM2 * ProjectedAreaRatio` (0.69; research 0.65 to 0.80; the plan's 0.72 put the zenith at
    1.12 kN); `ZeroLiftAngleDeg` 0 -> -3, `MaxLiftCoefficient` 1.2 -> 1.1, `StallAngleDeg` 18 -> 20,
    trims +2 / -22 -> +3.6 / -15.4 deg (throw 24 -> 19 deg of angle of attack), `ParkHoldGain` 2 ->
    2.5. 9 m^2 at the zenith in 30 kn 1656 -> 1090 N sheeted in, 371 N out
    (`Physics.ParkedAtZenith`); the 15 kn ride 14.7 kn 508 N -> 13.6 kn 407 N; bar in on the 9 m in
    20 kn 1120 -> 660 N (0.79 body weights, 3.6 times bar out: the plan's 1.0 to 1.4 and 4 times did
    not hold with the zenith target until item 3); timed jump 13.1 -> 12.2 m. 106 tests.
19. `c4179bb` **feat(kite): the assist judges the window by the horizontal wind** (item 2, A1). In
    the air the rider's climb and fall no longer turn the assist's frame. Timed jump 12.2 m in 3.62 s
    (`8h/t^2` 7.43) -> 12.8 m in 4.28 s (5.59), the kite no longer stalled on the way down
    (`Physics.AssistFrameIsHorizontal`). 107 tests.
20. `b0ac7e8` **feat(kite): in the air, bar centred flies the kite overhead** (A2).
    `SetRiderAirborne`, `AirborneZenithGain` 2, `AirborneZenithMaxHeadingDeg` 45. 13.3 m in 5.23 s,
    `8h/t^2` 3.87; the kite 66 deg above the rider on the way down, the lines holding up 0.63 body
    weights (`Physics.KiteOverheadInTheAir`). A loop from the apex peaks at 0.95 body weights against
    the research's 3 to 5: pinned as a gap (`Physics.AirborneLoopYanks`). 109 tests.
21. `4d33f9e` **feat(board): the send climbs the rider** (A3). `EdgeReleaseSeconds` 0.22 -> 0 (0 =
    physics only). The timed jump is flown with the jump button held and let go to pop (the loaded
    pop): 10.8 m in 4.80 s, `8h/t^2` 3.76. README heights 7 / 15 / 20 m -> 5 / 11 / 15 m in 15 /
    30 / 40 kn. 109 tests.
22. `041d1c6` **feat(board): edging from a force balance** (item 3). The rider heels the board to
    the balance of the pull across it (`bAutoEdge`, `MaxHeelDeg` 65, `LoadExtraHeelDeg` 25,
    `HeelResponse` 8); the water's normal force on the heeled board carries the pull; the fins
    (`FinAreaM2` 0.013) and the rail (`RailAreaM2` 0.08, `TailWeightRailScale` 4) make side force
    from leeway (`LateralLiftSlopePerRad` 2.5, `LeewayStallDeg` 12). The viscous grip and the drive
    made from it are gone. 15 kn ride 13.6 kn 407 N -> 15.0 kn 487 N, no leeway, heel 32 deg; 20
    deg above the beam reach 2.57 m/s made good; the closest planing course 55 deg above the beam
    reach (a gap, below). 114 tests.
23. `6f411fd` **feat(board): lift-off when the lines out-pull the loaded rider's weight.** The board
    leaves the water when the upward pull passes `m g (1 + LoadHoldBonus * load)` (`LoadHoldBonus`
    1.5); `LiftoffWeightFactor` and `EdgedLiftoffWeightBonus` are gone. Timed release 0.68 s: 10.7 m
    in 4.97 s; the kite pulls the rider off from 0.70 s. 114 tests.
24. `a616c28` **feat(debug): the board's heel and side forces** drawn at level 1 and logged at
    level 2 (`heel_deg`, `leeway_deg`, `side_n`, `normal_side_n`, `board_drag_n`).
25. `ab294ac` **feat(board): the carve's lean enters the force balance** (3c, `CarveHeelDeg` 35). A
    1.5 s full carve towards the kite turns the course 34 -> 81 deg with the heading at 80; the speed
    lost 21 -> 22% (the batch's target was under 20%). 115 tests.
26. `517b4d0` **feat(board): a floating rider is slow through the water** (3d,
    `FloatingDragAreaM2` 0.35 fading out by `FloatingDragFadeSpeedCmS` 200). The drift under a kite
    at 12 in 15 kn 2.2 -> 0.81 kn. 116 tests.
27. `be9e4b3` **feat(board): carrying the edge costs pressure drag, highest over the hump** (3b).
    On the plane the normal force leans back by the trim and drags the board by `N tan(trim)`,
    `PlaningTrimDeg` 6 at speed, rising to `PlaningTrimHumpDeg` 10 at the planing threshold from
    `PlaningTrimHumpSpeedCmS` 600 (the batch's 13 gave the same courses and left the big board stuck
    at the threshold in 12 kn); the speed terms re-based to keep 380 N at 10 m/s
    (`PlaningDragKgPerS` 8 -> 6.15, `PlaningQuadraticDragKgPerCm` 0.03 -> 0.0231). Closest
    sustainable course in 15 / 20 / 25 kn 55 / 60+ / 60+ -> 30 / 40 / 45 deg above the beam reach,
    best made good 3.95 / 4.68 / 5.23 -> 2.97 / 4.01 / 4.69 m/s (`Physics.CourseTheorem`,
    `Physics.PointingTooHighDropsOffThePlane`). The 15 kn ride 15.0 -> 13.9 kn; the 12 m^2 in 12 kn
    planes only with the bar right in (9.6 kn); the carve loses 27%; timed release 0.66 s. 117 tests.
28. `492bf40` **feat(board): read the water at five points under the board, fit a plane** (item 4,
    step 1). The centre, the nose and tail at `WaterSampleAlongFraction` (0.45) of the length, both
    rails at `WaterSampleAcrossCm` (18 cm); the plane's height, normal and the surface's vertical
    speed under the board (`GetWaterSurfaceHeightCm`, `GetWaterSurfaceNormal`,
    `GetSurfaceVerticalSpeedCmS`); the buoyancy damping acts on the speed relative to the surface.
    On a 1 m, 20 m swell at its steepest the board pitches 17.33 deg with the chord slope
    (`Water.FittedPlaneUnderTheBoard`). Nothing on flat water moved. 118 tests.
29. `afa95ab` **feat(board): the water holds the board by forces; landings in g, hot or crashed**
    (step 2). The 20 cm clamp is gone; a touchdown absorber takes a sink v out at `v^2 / (2 s)` over
    `LandingAbsorbDistanceCm` (30) times `1 + CrouchAbsorbBonus` (1) * the crouch; `LandingG = 1 +
    v^2 / (2 g s)` is what `OnBoardLanding` reports; hot over `HotLandingSinkMS` (6 m/s) or with the
    kite under `HotLandingKiteElevationDeg` (45); a crash over `CrashLandingG` (8) or
    `MaxLandingAngle`. The crouch builds in the air. 2 / 4 / 6 m/s -> 1.66 / 3.67 / 7.05 g standing,
    4.02 g crouched at 6 m/s (`Physics.LandingGFromSink`); across a 1 m, 20 m swell at 15 m/s never
    more than 15 cm under for longer than 0.133 s (`Physics.RidesASwellWithoutTunnelling`); the timed
    jump lands at 6.68 g crouched, hot, sinking 8.17 m/s (`Physics.GoodLandingIsThreeToSixG`: the 3 to
    6 g target is not met, pinned, below). 121 tests.
30. `7c2fc73` **feat(hud): a landing card with the landing's g and HOT** ("LANDED 4.2 g", "HOT",
    "CRASH 9.2 g", 3 s, `HUD.LandingCard`); `kite.Physics.Debug 1` draws the five water samples and
    the fitted normal, `2` logs `water_z`, `surface_vz`, `water_up_n`, `absorbing`. 122 tests.
31. `99de798` **fix(board): the loaded rider's legs hold the board on the water until lift-off.**
    Without the clamp a loaded rider pulled up harder than they weigh rose off the water before the
    lift-off rule let them go, and the best jump in 15 kn grew from 4.5 to 8.1 m and crashed on
    landing. 20 cm above the ride height the legs now hold the board down with up to `LoadHoldBonus *
    load * m g` and stop its rise there; the release sweeps are the clamp's again (15 kn 4.4 m, 30 kn
    10.7 m at 0.66 s). 122 tests.
32. **docs(physics)**: this section, `docs/movement.md`, `docs/jumping.md`, `docs/ARCHITECTURE.md`,
    `README.md` and the status of each item in `plan-2.md`.
33. **merge: origin/main into physics/phase2.** Main's #46 to #70 on top of phase 2. The trick
    tracker (#69) takes the board's landing sink and g instead of its own `-Vz` over 30 cm, so the
    trick card and the landing card show one number; the board's `LandingGForSink` is
    `LandingMath::ComputeLandingG` (#64) and `GetLandingCount` is `GetJumpCount` (#46). The wind to
    90 kn and the ceiling at the clouds (#46) need nothing more: the highest storm jump is 24 m with
    the kite about 20 m above the rider, where the power-law profile gives 1.1 to 1.2 of the 10 m
    wind, so it is not clamped. The visual board (#68) hangs off the root, so the fixed
    step's drawing between steps moves it. Main's jumps are flown the phase 2 way (the jump button held
    and let go at 0.66 s in 30 kn, 0.2 s in a storm, a crouched landing). Storm jumps: 21.2 m and
    156 m in 60 kn, 22.4 m and 225 m in 90 kn, both a crash on landing (`storm-jumps.md`). 180 tests.

### Where the numbers stand

| Scenario (test) | At `13f8e13` | Now | Real-world ballpark (`research.md`) |
| --- | --- | --- | --- |
| 12 m^2, 81 kg, 15 kn, 30 s (`Physics.SteadyRideAcross`) | 14.7 kn, 508 N (0.64 BW) | 13.9 kn, 455 N (0.57 BW), heel 29 deg, no leeway | 12 to 25 kn; riding pull 0.5 to 1.0 BW |
| 20 kn, the kite a rider rigs (9 m^2; was the 12 m^2) | 19.5 kn, 844 N (1.06 BW) | 17.2 kn, 556 N (0.70 BW) | as above |
| 20 deg above a beam reach, 15 kn (`Physics.UpwindAtEdgeAngle`) | 25 deg up: 1.72 m/s made good, leeway 9.6 deg | 2.32 m/s at 13.2 kn, no leeway (20 kn, 9 m^2: 2.88 m/s) | 2 to 3 m/s |
| Closest sustainable course, 15 kn (`Physics.CourseTheorem`) | any course, held by grip with leeway | 30 deg above the beam reach, 2.97 m/s made good (20 kn 40 deg, 25 kn 45 deg) | 15 to 25 deg |
| 9 m^2 at the zenith, 30 kn (`Physics.ParkedAtZenith`) | in 1656 N, out 359 N | in 1090 N, out 371 N | in 0.9 to 1.0 kN, out about 380 N |
| 15 -> 22 kn gust (`Physics.GustHitsMidRide`) | 506 -> 978 N, 14.6 -> 21.1 kn | 448 -> 1044 N, 13.8 -> 22.9 kn | pull with v^2: x2.15 |
| Full-bar loop, standing (`Kite.LoopRadiusIsSpeedIndependent`) | 6.3 m in 2.28 s at 15 kn, 6.5 m in 1.43 s at 25 kn | 6.54 m in 2.47 s, 6.77 m in 1.53 s | radius independent of speed |
| 9 m^2, 20 kn, bar out / half / in (`Ride.BarIsTheThrottle`) | 9.0 kn 172 N / 13.8 kn 367 N / 21.7 kn 1120 N | 6.0 kn 161 N / 13.6 kn 376 N / 20.8 kn 849 N (1.02 BW, 5.3 x out) | depowered about 0.5 BW, powered 1 to 1.3 BW |
| 12 m^2 in 12 kn (`Ride.SpeedScalesWithWind`) | 11.8 kn at 0.7 of the bar | off the plane at 0.7; 9.6 kn with the bar in | a 15 to 17 m^2 day for 85 kg |
| Timed jump, 30 kn, 6 m^2 (`Jump.TimedReleaseBeatsPop`, `Physics.HangTime`) | 13.1 m in 3.67 s | 10.7 m in 4.98 s | 10 to 20 m; 5 to 7 s for 15 m |
| Effective gravity `8h/t^2` | 7.8 m/s^2 | 3.46 m/s^2 | 1.5 to 3.5 m/s^2 |
| The kite on the way down (`Physics.KiteOverheadInTheAir`) | 19 deg above the rider, stalled, lifting 10% | 71 deg (lowest 56), unstalled, lifting 69% | overhead, about 1 BW |
| Landing g at 2 / 4 / 6 m/s sink (`Physics.LandingGFromSink`) | none (the sink over g, a time) | 1.66 / 3.67 / 7.05 g standing; 1.33 / - / 4.02 g crouched | 4 m/s into 0.3 m is 3.7 g |
| The timed jump's landing (`Physics.GoodLandingIsThreeToSixG`) | not measured as a g | 6.68 g crouched, sinking 8.17 m/s, hot; standing it would be 12.3 g, a crash | 3 to 6 g (measured 4.2 to 5.5); sink 3 to 6 m/s |
| A 1 m, 20 m swell at 15 m/s (`Physics.RidesASwellWithoutTunnelling`) | held within 20 cm by the clamp | at most 21.8 cm under, over 15 cm for at most 0.133 s; off the water over each crest | no tunnelling |
| Floating under a kite at 12, 15 kn (`Physics.FloatingRiderIsSlowThroughTheWater`) | 0.3 kn (at `4d33f9e`) | 0.81 kn | a sunk rider barely moves |
| Pop with the kite parked | 1.5 m | 1.0 m | under 1 m plus kite lift |
| Same ride at 30, 60, 120 fps (`Physics.StepRateIndependent`) | identical | identical | identical |

### Against the 13f8e13 feel

What a player who knew `13f8e13` will notice, and which way it went:
- **Less pull everywhere** (the projected area): the zenith a third lower, the 15 kn ride 508 -> 455 N.
- **Slower riding**, more so the slower the board: 14.7 -> 13.9 kn in 15 kn; bar out 9 -> 6 kn on the
  9 m in 20 kn; the 12 m^2 in 12 kn only planes with the bar right in; from a standstill in 15 kn
  the board planes after about 17 s. The pressure drag near the planing hump does this.
- **Upwind is honest**: the board holds the course it is pointed with no leeway and makes 2.3 m/s
  good 20 deg up, but cannot hold more than 30 deg above the beam reach (it used to hold any course
  with a few degrees of leeway).
- **Carving** turns the course about as far (82 deg in a 1.5 s full carve towards the kite, 72
  before item 3) but costs speed: 27% lost, where the manufactured drive used to add 9%.
- **A sunk rider** drifts a little more under a kite at 12 (0.8 kn against 0.3; 2.2 kn between item 3
  and its floating drag).
- **Jumps are lower and floatier**: 13.1 -> 10.7 m at 30 kn but 3.7 -> 5.0 s in the air; 4.4 m in
  15 kn, about 13 m that can be landed in 40 kn. The kite flies overhead in the air by itself.
- **Landings cost something**: a big jump has to be landed crouched (the jump button held again on
  the way down); the timed jump lands hot at 6.7 g; landed standing it is a crash; the biggest jumps
  in 40 kn crash even crouched.

## What to tune

The tunables that move the riding and jumping most, with today's default and the value to tune
towards. "Estimate" values in `research.md` are starting points, not requirements.

| Tunable | Default | Research value to tune towards | Notes |
| --- | --- | --- | --- |
| `UKiteComponent::ProjectedAreaRatio` | 0.69 | 0.65 to 0.80 (0.72 typical) | Sets the zenith pull and every riding pull with it; 0.72 puts the zenith at 1.12 kN. |
| `UKiteComponent::ZeroLiftAngleDeg` | -3 deg | -3 deg | At research value. |
| `UKiteComponent::MaxLiftCoefficient` / `StallAngleDeg` | 1.1 / 20 deg | 1.0 to 1.2 at 16 to 20 deg | The 2 deg of stall margin keeps the kite flying on the way down. |
| `UKiteComponent::TrimSheetedOutDeg` / `TrimSheetedInDeg` | -15.4 / +3.6 deg | a bar throw of 12 to 20 deg of angle of attack | 19 deg today: bar in 5.5 deg short of the stall at the window edge in 20 kn, bar out -4.5 deg. |
| `UKiteComponent::GravityTurnRadMPerS2` | 2.4 rad m/s^2 (12 m^2) | 6.28 (Fechner c_2, 10 m^2) | Above 2.4 raise `ParkHoldGain` (2.5) with it. |
| `UKiteComponent::SteeringDragFactor` | 0.6 | 0.6 (Fechner) | At research value. |
| `UKiteComponent::SteeringDeadTimeSeconds` / `DepoweredDeadTimeExtraSeconds` | 0.15 s / 0.3 s | 0.2 s powered to 0.6 s depowered (Elfert 2024) | Delays the rider's bar; the assist is not delayed. |
| `UKiteComponent::DepoweredTurnRateFactor` | 0.45 | 0.43 | |
| `UKiteComponent::MinTurnRadiusCm` | 420 cm at 12 m^2 | g_k 0.15 to 0.35 rad/m | Scales with sqrt(area). |
| `UKiteComponent::AirborneZenithGain` / `AirborneZenithMaxHeadingDeg` | 2.0 / 45 deg | the kite overhead within about a second of take-off | Feel; 0 gain is the phase 1 behaviour in the air. Where the descent gap (below) would be worked on. |
| `UKiteComponent::MaxLineTensionN` | 6000 N | line stretch of 1 to 1.5% (compliant lines, deferred) | Stands in for line compliance. |
| `UBoardMovementComponent::EdgeReleaseSeconds` | 0 s | none: a pseudo-impulse | 0.22 is the phase 1 jump. |
| `UBoardMovementComponent::PopImpulseKgCmPerS` / `LoadPopBonus` / `TailWeightPopBonus` | 21000 kg cm/s (2.5 m/s) / 0.6 / 0.5 | 1 to 2 m/s from the legs | The loaded pop with the weight back is 5.9 m/s; the 30 kn jump leans on it (known gaps). |
| `UBoardMovementComponent::LoadHoldBonus` | 1.5 | 1.5 to 3 (take-off at 2.5 to 4 body weights of tension) | More hold widens the release window but does not raise the best jump. |
| `UBoardMovementComponent::bAutoEdge` / `MaxHeelDeg` / `LoadExtraHeelDeg` / `HeelResponse` | on / 65 deg / 25 deg / 8 /s | lean 30 to 60 deg riding | |
| `UBoardMovementComponent::CarveHeelDeg` | 35 deg | `atan(v w / g)` for the turn rate: 35 deg at 6.5 m/s and 60 deg/s | |
| `UBoardMovementComponent::FinAreaM2` / `RailAreaM2` / `TailWeightRailScale` | 0.013 / 0.08 m^2 / 4 | fins 4 x 0.003 to 0.005 m^2; rail about 0.04 m^2 at full heel; no value for the tail | The rail and the tail scale hold the edge through the send: the timed jump is 9.5 m at the research's values. |
| `UBoardMovementComponent::LateralLiftSlopePerRad` / `LeewayStallDeg` | 2.5 /rad / 12 deg | 2 to 3 /rad | |
| `UBoardMovementComponent::PlaningTrimDeg` / `PlaningTrimHumpDeg` / `PlaningTrimHumpSpeedCmS` | 6 / 10 deg / 600 cm/s | trim 6 to 10 deg (Savitsky) | The hump bounds the course at 30 deg and slows light-wind riding; 13 deg strands a board at the planing threshold. |
| `UBoardMovementComponent::PlaningDragKgPerS` / `PlaningQuadraticDragKgPerCm` | 6.15 kg/s / 0.0231 kg/cm | `a + c v^2` with a about 60 N, c 0.5 to 1.0 N s^2/m^2 | Today's 6.15 v + 2.31 v^2 N plus the pressure drag is 380 N at 10 m/s, two to three times the research's: it sets riding speed against the kite. |
| `UBoardMovementComponent::FloatingDragAreaM2` / `FloatingDragFadeSpeedCmS` | 0.35 m^2 / 200 cm/s | a sitting rider and a sunk board | |
| `UBoardMovementComponent::LandingAbsorbDistanceCm` / `CrouchAbsorbBonus` | 30 cm / 1.0 | 0.2 to 0.4 m of legs and immersion | The crouch doubles it; the knobs for landing feel with `CrashLandingG`. |
| `UBoardMovementComponent::CrashLandingG` | 8 g | measured landings 4.2 to 5.5 g | With 30 cm standing, a crash past 6.4 m/s of sink; crouched, past 9.1 m/s. |
| `UBoardMovementComponent::HotLandingSinkMS` / `HotLandingKiteElevationDeg` | 6 m/s / 45 deg | a descent of 3 to 6 m/s under a kite held overhead | The flag only; not a crash. |
| `UBoardMovementComponent::WaterSampleAlongFraction` / `WaterSampleAcrossCm` | 0.45 / 18 cm | the hull's wetted footprint | |
| `UBoardMovementComponent::RiderDragAreaM2` | 0.7 m^2 | 0.5 to 1.0 m^2 | |
| `UWindComponent::ShearExponent` / `GustStrength` / `DirectionDriftDeg` | 0.11 / 0.3 / 5 deg | 0.11 +- 0.03; gust factor 1.23 at sea; 4 to 6 deg | |
| `AKiteRiderPawn::SimStepSeconds` | 1/240 s | 1/240 s (1/120 would do) | |

Removed in phase 2: `BaseGripKgPerS`, `EdgeGripKgPerS`, `EdgeDriveEfficiency`, `TailWeightGripScale`,
`LoadGripBonus`, `AutoHeelDeg`, `AutoHeelFullLoadN`, `TailWeightHeelDeg` (item 3: the force
balance), `LiftoffWeightFactor`, `EdgedLiftoffWeightBonus` (item 3: lift-off from the loaded hold),
and the 20 cm water-contact clamp (item 4).

## Known gaps

- **The descent and the landing** (plan-2 items 2 and 4). The timed jump at 30 kn comes down at 8.2 m/s
  where the research has 3 to 6 m/s under a kite held overhead: over the last 1.75 s of the flight
  the kite, held at clock 0 by the airborne assist, sinks from 84 to 55 deg above the rider and
  falls faster than they do, and the lines' upward pull drops from 0.8 to 0.5 body weights. So the
  jump lands hot at 6.7 g crouched (target 3 to 6 g; standing it would be 12.3 g, a crash), and the
  bigger the jump the faster the sink: in 40 kn the best release goes 15.9 m and lands at 10.3 m/s,
  10 g, a crash even crouched; the highest that lands is 13.1 m. The landing model is not what is
  short (`Physics.LandingGFromSink`); the airborne kite has to hold the rider up on the way down,
  sheeted in and kept overhead. `Physics.GoodLandingIsThreeToSixG` pins the 6.7 g.
- **Course limit** (item 3b). The closest course the board holds while planing is 30 deg above the
  beam reach in 15 kn (research 15 to 25), 40 in 20 kn and 45 in 25 kn, with 2.97 / 4.01 / 4.69 m/s
  made good (research 2 to 3). The kite's drive along the course barely falls as the board slows:
  with the park-hold assist holding it where it is, it is 157 to 171 N on a course 30 deg up in
  15 kn from 3 to 12 m/s of board speed, so only drag that rises steeply as the board slows stops
  it, and the pressure drag over the planing hump is that drag. A kite flown by hand would be
  sheeted and moved and lose its drive as the board slows. The same hump drag makes light-wind
  riding harder (12 m^2 in 12 kn planes only with the bar right in). `Physics.CourseTheorem` pins
  25 to 40 deg.
- **Carve speed loss** (item 3c): a 1.5 s full carve towards the kite loses 27% of its speed (target
  under 20%; 22% before the pressure drag). The course comes round to nearly straight at the kite,
  the lines go light, and the hull's drag and the pressure drag of the 39 deg heel slow the board.
  `Physics.CarveFollowsTheHeading` bounds it at 30%.
- **Loop yank in the air** (item 2). A loop flown from the apex of the timed jump peaks at 0.9 body
  weights with the kite still 66 deg up, against the research's 3 to 5 with the kite low: at the
  apex the kite flies at about 14 m/s of air with a 3 m turn on 24 m lines and stays near the top.
  `Physics.AirborneLoopYanks` pins 0.9.
- **The loaded pop carries the jump.** The 30 kn timed jump needs the jump button's loaded pop, 5.9
  m/s from the legs with the weight back (research 1 to 2 m/s): with a tap the same send gave 7.4 to
  8 m at item 2. Its window is narrow: 0.66 s is the best release, from 0.70 s the kite pulls the
  rider off at 2.5 body weights of upward pull for a 4.9 m jump.
- **Water contact**: a board that leaves the water without a jump (over a swell crest) stays in its
  on-water state, with the hull's horizontal forces still on, until it comes back down; the loaded
  hold stops the board's rise 20 cm above the water (a stop, as the clamp was, not a force from the
  water); the Water plugin's waves advance once a frame, so on them the surface's vertical speed
  comes in steps at the first fixed step of a frame.
- **Research values not at their defaults:** `GravityTurnRadMPerS2` (2.4, research 6.28),
  `PopImpulseKgCmPerS` (2.5 m/s, research 1 to 2), the rail area and its tail scale (tuned for the
  jump), the planing drag (two to three times the research's).
- **Deferred** (plan-2 item 5): compliant lines at the real stiffness (`MaxLineTensionN` stands in);
  the world wind subsystem with water darkening; a relaunch that has to be flown; line sag in the
  visuals; rider rotation.
- **Hitches.** With the defaults the frame clamp (0.1 s) binds before `MaxSimStepsPerFrame`, and
  0.1 s in float holds a hair under 24 steps, so a long frame runs 23 and the 24th waits for the
  next frame. Harmless, noted for whoever tunes the clamp.
- **Nothing here was checked in a `-game` run**, including the debug drawing and the landing card.
