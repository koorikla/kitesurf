# Physics plan, phase 2

Phase 1 (`plan.md`, merged as PR 45) put a fixed step, exact drag, a real wind field, the
bar-centred drift to 12 and the research's steering terms under the game, and left four things
open in `CHANGELOG.md`: the kite pulls too hard at the zenith because it flies on its flat area;
jumps are too short for their height because the kite is not kept overhead and the climb is a
pseudo-impulse; edging is viscous drag plus a manufactured drive; and the water is a 20 cm clamp
with a landing number that is not a g. This plan closes them, in that order, each with the
physics it comes from (`research.md` section numbers in brackets), what changes, how it is
verified and what it risks.

The feel at `main` commit `13f8e13` was judged fine by the user. Every item below moves numbers;
each commit records what moved against that baseline, and the old feel stays reachable through
the tunables listed under "Interfaces".

Ground rules are unchanged: C++ for the physics; every tuning value a `UPROPERTY` with its unit;
SI inside the step with one conversion at the boundary (`KiteSurfUnits.h`); the pawn's fixed
step owns all integration; existing inputs, console commands, HUD getters and test entry
points keep working; one concern per commit, built and tested before it.

## 1. Projected area, camber and trim: calibrate the kite's pull [research 1.2, 1.3, 1.7]

**Status: done with deviations** (`e3e112b`). `ProjectedAreaRatio` is 0.69, not 0.72: at 0.72 the
zenith is 1.12 kN. The bar-in row (1.0 to 1.4 body weights, 4 times bar out) did not hold with the
zenith row at this item (0.79 and 3.6); it holds since item 3 (1.02 and 5.3). `ParkHoldGain` 2 ->
2.5 to keep the parked kite on its clock. Trims +3.6 / -15.4 deg (throw 19 deg).

**Problem.** `UKiteComponent` uses `AreaM2` (the flat area a kite is sold by) in every force.
Lift and drag act on the projected area, 0.65 to 0.80 of flat (typical 0.72). The result is
1.65 kN at the zenith for 9 m^2 in 30 kn against the research's 0.9 to 1.0 kN, and a jump that
starts from too much pull. The attached-flow curve also has `Cl(0) = 0` (`ZeroLiftAngleDeg` 0),
so the bar range is doing camber's job.

**Change.**
- `ProjectedAreaRatio` (0.72, 0.65 to 0.80) on the kite. Aerodynamic forces use
  `AreaM2 * ProjectedAreaRatio`; mass, added mass, turn radius and the mesh keep using the flat
  area as today. `GetProjectedAreaM2()` for the debug text and tests.
- Camber `ZeroLiftAngleDeg` -3 (research), `MaxLiftCoefficient` 1.1 and `StallAngleDeg` 20
  (research 1.0 to 1.2 at 16 to 20 deg; the extra 2 deg of stall margin matters in item 2's
  descent, where the sink speed raises the angle of attack).
- Re-base `TrimSheetedInDeg` / `TrimSheetedOutDeg` by measurement so that, parked at the window
  edge in 20 kn, sheet 1 gives an angle of attack 5 to 6 deg below the stall and sheet 0 gives
  about -5 deg (a luff margin, not a collapse). The bar throw should come out at 15 to 20 deg
  of angle of attack (research 12 to 20).

**Calibration targets** (all from tests; move the three trims and the ratio, nothing else,
until all hold):

| Scenario | Target |
| --- | --- |
| 9 m^2 at the zenith in 30 kn, sheet 1 (`Physics.ParkedAtZenith`) | 0.85 to 1.1 kN, not stalled |
| same, sheet 0 | under 0.45 kN, lines taut |
| 12 m^2, 81 kg, 15 kn, 30 s ride (`Physics.SteadyRideAcross`) | pull 0.5 to 1.0 body weights, 12 to 25 kn |
| 9 m^2, 20 kn, bar in / out (`Ride.BarIsTheThrottle`) | in 1.0 to 1.4 BW; in / out tension ratio at least 4 |
| 7 vs 14 m^2 parked (`Kite.SizeForWind`) | linear in area, as now |

**Verify.** The targets above as assertions; every existing kite, ride, jump and gear test; the
commit body lists each number before and after against `13f8e13`. If the light-wind board test
(`Gear.ChangesBehaviour`, the 132 does not plane in 12 kn) cannot hold with the research
values, re-base it and say so: it encodes a gameplay distinction, not a physical law.

**Risk.** Medium: every pull and speed number moves by about the area ratio. Mitigated by
doing it first, so items 2 to 4 calibrate against the corrected pull.

## 2. The kite in the air: assist frame, overhead hold, the send does the climbing [research 1.1, 3.3]

**Status: done with deviations** (`c4179bb`, `b0ac7e8`, `4d33f9e`). A4 was not needed (no stall on
the way down) and `AirborneTrimEaseDeg` was not added. The timed jump is flown with the jump button's
loaded pop: with a tap the send reached 7.4 to 8 m, under the 10 m. `KiteOverheadInTheAir` allows
2.5 s, not 2 s, to get above 60 deg on 11 to 15 m jumps (the rider climbs faster than the kite can).
`AirborneLoopYanks` is a known gap, pinned at 0.9 body weights against 3 to 5. The descent is a known
gap found by item 4: the kite sinks from 84 to 55 deg in the last 1.75 s and the rider lands at
8.2 m/s.

**Problem** (from the phase 1 diagnosis, `CHANGELOG.md`). After take-off the kite sits low and
to the side of the rider and lifts about 10% of their weight on the way down; `8h / t^2` is
7.8 m/s^2 against 1.5 to 3.5. Three causes:
1. The assist's idea of "out of the window" uses the rider's full 3D velocity
   (`ComputeSteering`: `WindFelt = Wind - RiderVelocity`). In the air the rider's climb and
   fall are as large as the wind, so the assist's frame rotates with them and its command
   flips from side to side. A rider's sense of the window is the horizontal wind.
2. Nothing asks the kite to be overhead in the air; the on-water drift to 12 (gain 1.0, 20 deg)
   is too slow for a 4 s flight.
3. 5.7 of the 10 m/s take-off speed is `EdgeReleaseSeconds * F_kite,z`, a pseudo-impulse. The
   research's mechanism is that the edge stops holding and the already large, near-vertical
   tension accelerates the rider; the kite keeps doing work through the climb.

**Change.**
- **A1, assist frame.** The assist (and only the assist: the aerodynamics keep the true 3D
  airflow) uses the horizontal apparent wind: `WindFelt = Wind - (RiderVelocity.X, .Y, 0)`.
  `GetClockDeg`, `GetWindowDepthDeg` and `GetWindowAxis` already use the horizontal axis. The
  floor rule keeps the real vertical relative velocity (it predicts the kite hitting the
  water).
- **A2, overhead hold in the air.** The pawn tells the kite each step whether the rider is
  airborne (`SetRiderAirborne(bool)`, from the board state). In the air with the bar centred the
  assist's target is clock 0 over the rider with `AirborneZenithGain` (2.0 deg per deg) up to
  `AirborneZenithMaxHeadingDeg` (45 deg): the kite is overhead within about a second of
  take-off and stays there. Bar over still travels, bar towards the kite's side still loops
  (the heli-loop and the redirect stay the player's), and the floor rule stays.
- **A3, the send climbs the rider.** `EdgeReleaseSeconds` defaults to 0 (kept as a tunable,
  documented as "0 = physics only"); the pop is the leg impulse alone. Height then comes from
  the line force acting on the point mass after the edge lets go. Lift-off keeps today's rule
  (item 3 replaces it): the board leaves the water when the upward line force exceeds
  `LiftoffWeightFactor + EdgedLiftoffWeightBonus * EdgeHold` body weights, where `EdgeHold`
  includes `LoadAmount` from the load button. With the kite sent up through 12 at speed the
  tension at release is 2.5 to 4 body weights and near vertical, which gives 1.5 to 3 g of
  climb for the first second: 10 to 20 m without any impulse.
- **A4, descent.** With item 1's stall margin the kite overhead should keep flying as the sink
  speed raises its angle of attack. If the trace still shows a stall during the descent, the
  airborne assist may ease the trim target (not the player's bar) by up to
  `AirborneTrimEaseDeg` (0 by default; a knob, not a default behaviour) and the commit says so.

**Verify.**
- `KiteSurf.Physics.HangTime`: the timed jump at 30 kn on the recommended kite is 10 to 20 m
  with `8h / t^2` between 1.5 and 5 m/s^2 (real 1.5 to 3.5; 5 is the ceiling until item 4's
  landing model) and airtime at least 5 s for a jump over 12 m.
- `KiteSurf.Physics.KiteOverheadInTheAir`: bar centred after take-off, kite elevation above
  60 deg within 2 s and above 50 deg until 1 s before touchdown; vertical line force averaged
  over the flight is 0.55 to 0.9 body weights.
- `KiteSurf.Physics.AirborneLoopYanks` (research C6): a loop started at the apex of a jump over
  10 m peaks at 3 to 5 body weights with line elevation under 30 deg, and the kite is back
  above 60 deg before touchdown on jumps of 15 m or more.
- `KiteSurf.Jump.TimedReleaseBeatsPop` and main's `Jump.LoadAndRelease` re-based: timed beats
  send-only beats pop still holds; the numbers move and are listed.
- `KiteSurf.Physics.AssistFrameIsHorizontal`: a rider moving vertically at 5 m/s with no
  horizontal speed in 15 kn gets the same assist command as one standing still.

**Risk.** Medium-high on jump feel (heights and timing change); low on riding (A1 changes
nothing on the water where the rider's vertical speed is near zero). The pseudo-impulse is
kept as a tunable so the phase 1 feel is one value away.

## 3. Edging from a force balance, with the edge separated from heading [research 2.3, 3.1]

**Status: done with deviations** (`041d1c6`, `6f411fd`, `a616c28`, and the follow-ups `ab294ac`
carve lean, `517b4d0` floating drag, `be9e4b3` pressure drag). `FinAreaM2` 0.013 (not 0.04: the fins
alone held a flat board) and `TailWeightRailScale` 4 (not 1.5: the edge through the send); the side
force acts across the board's axis; the normal force's sideways part is scaled by how far the rider
is up on the board. `LoadingBuildsTension`: the kite sits deeper while the tension builds, not
towards the edge. The course limit is 30 deg above the beam reach in 15 kn (the research's 15 to 25
is not met; `CourseTheorem` asks 25 to 40), with the pressure drag `N tan(trim)` and a trim of
10 deg over the hump (the batch's 13 left a board stuck at the planing threshold); the carve loses 27%
of its speed (target under 20%); light-wind planing is harder.

**Problem.** Sideways grip is viscous (`F = c * v_lat`), forward drive is manufactured from it
(`|F_lat| * edge * 0.35`), heel is cosmetic, and the carve input both turns the board and sets
grip. Upwind ability comes from a coefficient with no physical meaning. Main's #41 added a load
button (`LoadAmount`, hold the jump button) that raises grip and holds the rider down; that is
the edge-hold lever this item needs, so no new input asset is required.

**Model.** On the water, per fixed step, in SI:
- **Heel** `HeelDeg` is state, positive towards the kite's side. Its target is the balance
  heel that carries the kite's sideways pull through the board's normal force,
  `phi_bal = atan2(T_lat, max(m g - T_v, eps))` (research 3.1), when `bAutoEdge` is on (default
  on: it is what a rider's body does without thinking), plus `LoadAmount * LoadExtraHeelDeg`
  (25 deg: loading is edging harder than the balance needs), clamped to `MaxHeelDeg` (65).
  `HeelDeg` follows the target at `HeelResponse` (8 /s). The carve input's roll
  (`SmoothedCarveInput * MaxEdgeAngleDeg`) stays for the visual and is added to the drawn roll,
  not to `HeelDeg`.
- **Water normal force.** `N = max(m g - T_v, 0) / cos(phi)`; its horizontal component
  `N sin(phi)` acts across the board axis towards the heel side, against the pull. At the
  balance heel it carries `T_lat` exactly and no leeway builds; loading beyond it pushes the
  rider upwind of the pull, which is the loading mechanism (the apparent wind goes aft, the kite
  sits deeper, tension rises).
- **Side force from leeway.** Leeway `beta` is the angle between the horizontal velocity and
  the board axis. `F_side = 0.5 rho_w v^2 A_lat C_L,beta * clamp(beta, +-LeewayStallDeg)`,
  with `A_lat = FinAreaM2 + RailAreaM2 * sin(phi) * railScale(weightShift)`: `FinAreaM2` 0.04
  (four 4.5 cm fins), `RailAreaM2` 0.08 at full heel, `LateralLiftSlopePerRad` 2.5,
  `LeewayStallDeg` 12, `TailWeightRailScale` 1.5 (weight back digs the rail in; weight forward
  gets the inverse). The force is perpendicular to the horizontal velocity, so its drag
  component (`F_side sin(beta)`) comes for free.
- **Drive.** None manufactured. The kite's pull along the course drives; the existing
  `a + c v^2` drag resists; the side force and the tilted normal force act across. Upwind ability
  then follows from the two drag angles (the course theorem, research 2.3).
- **Lift-off.** The board leaves the water when `T_v > m g * (1 + LoadHoldBonus * LoadAmount)`
  (`LoadHoldBonus` 1.5: a crouched, loaded rider hangs on to 2.5 body weights, research
  take-off 2.5 to 4). `LiftoffWeightFactor` and `EdgedLiftoffWeightBonus` go.
- **Heading** is unchanged: carve yaws the board at `CarveTurnRate`; switch-stance and the
  low-speed pivot stay. The velocity now follows the heading through the side force, with a
  few degrees of leeway, instead of being dragged onto it.
- Removed: `BaseGripKgPerS`, `EdgeGripKgPerS`, `EdgeDriveEfficiency`, `TailWeightGripScale`,
  `LoadGripBonus`. `GetEdgeInput`, `SetWeightShift`, `SetLoadHeld`, `GetLoadAmount` keep
  working. `FBoardStepDebug` gains heel, leeway, side force, normal side force.

**Verify** (research C2 and the course theorem):
- `KiteSurf.Physics.FlatBoardSlidesDownwind`: auto-edge off, heel 0, kite parked: leeway over
  20 deg within 5 s.
- `KiteSurf.Physics.BalancedEdgeHoldsCourse`: auto-edge on, beam reach, 15 kn: leeway under
  5 deg, 12 to 25 kn, heel 25 to 55 deg.
- `KiteSurf.Physics.UpwindAtEdgeAngle` (existing): 2 to 3 m/s made good at 15 to 20 kn.
- `KiteSurf.Physics.BeamReachIn25kn`: 18 to 25 kn, course 15 to 25 deg above the beam reach.
- `KiteSurf.Physics.CourseTheorem`: the closest course the rider holds while planing is within
  8 deg of `atan(1/LD_kite) + atan(1/LD_board)` from the apparent wind, with both ratios read
  from the debug structs.
- `KiteSurf.Physics.LoadingBuildsTension`: holding load for 1.5 s raises tension by at least
  25% and moves the kite towards the window edge; a pop from the load beats an unloaded pop.
- All ride, gear and jump tests re-based with before and after against `13f8e13`.

**Risk.** High: every riding number moves. Own batch; calibration pass with the debug CSV
before the tests are re-based.

## 4. Water contact, landing g and hot landings [research 3.4, 2.2]

**Status: done with deviations** (`492bf40`, `afa95ab`, `7c2fc73`, `99de798`). The swell test came
with the clamp's removal, not with the sampling (with the clamp it sat at 20 cm for 0.55 s). No
extra planing lift was needed. The crouch builds in the air so that it can soften a landing. A loaded
rider's legs hold the board on the water until lift-off and stop its rise 20 cm above it, as the
clamp did (without it the late releases gained and the best jump in 15 kn crashed). Target not met:
the timed jump with the kite overhead lands at 6.7 g crouched and hot (3 to 6 g asked), because the
descent is 8.2 m/s; `GoodLandingIsThreeToSixG` pins it.

**Problem.** The vertical axis is a kinematic 20 cm clamp with `Vz = 0`; one water sample under
the pawn; wave orbital velocity discarded; "landing g" is `|Vz| / g`, a time.

**Change.**
- **Five samples** (centre, nose, tail, both rails at 0.45 L and 0.18 m) through the existing
  `IKiteWaterSurface`; a plane fitted to them gives height and normal; the surface's vertical
  velocity at the centre from the height change per step.
- **The clamp goes.** The vertical axis is forces only: the buoyancy spring about the ride
  height (exists), planing lift (exists), and a touchdown absorber: when the board meets the
  surface with relative sink `v_rel`, a constant deceleration `v_rel^2 / (2 s)` acts over the
  absorb distance `s = LandingAbsorbDistanceCm` (30 cm, times `1 + CrouchAbsorbBonus *
  LoadAmount` so a crouch softens it, bonus 1.0) until the relative vertical speed is zero.
  `LandingG = 1 + v_rel^2 / (2 g s)` is reported by `OnBoardLanding` and the HUD.
- **Verdicts.** Clean landing: angle under `MaxLandingAngle` and `LandingG` under
  `CrashLandingG` (8). A **hot landing** flag (not a crash by itself) when sink exceeds
  `HotLandingSinkMS` (6 m/s) or kite elevation is under `HotLandingKiteElevationDeg` (45).
  Crash recovery unchanged.
- The planing / displacement regimes, the float depth and the sand are unchanged.

**Verify.**
- `KiteSurf.Physics.LandingGFromSink`: landings at 2 and 6 m/s sink give `LandingG` of about
  1.7 and 7.1 (ratio of the squares), a crouch lowers both.
- `KiteSurf.Physics.GoodLandingIsThreeToSixG` (research C7): the timed jump with the kite
  overhead lands at 3 to 6 g and rides away; with the kite under 45 deg it is flagged hot.
- `KiteSurf.Physics.RidesASwellWithoutTunnelling` (research C3): a 1 m, 20 m sine swell at
  15 m/s, the board is never more than 15 cm under the local surface for longer than 0.2 s,
  and pitch follows the slope.
- `Jump.CleanLanding`, `Jump.CrashRecovery` and the audio landing test re-based.

**Risk.** Medium: landing feel and crash frequency change; the absorb distance and crash g are
the knobs.

## 5. Deferred again

**Status: deferred.**

- Compliant lines at the real stiffness (phase 1 item 11): `MaxLineTensionN` stands in.
- World wind subsystem with water darkening (phase 1 item 13): visual.
- A relaunch that has to be flown; line sag in the visuals; rider rotation.

## Order, batches and gates

| # | Item | Fixes | Size | Risk | Gate |
| --- | --- | --- | --- | --- | --- |
| 1 | Projected area, camber, trim | zenith pull; the base for 2 to 4 | M | medium | calibration targets, all tests |
| 2 | Airborne assist, overhead hold, send climbs | hang time | M | medium-high | HangTime 1.5 to 5 m/s^2, overhead, loop yank |
| 3 | Force-balance edging | edging, upwind, lift-off | L | high | course theorem, beam reach, loading |
| 4 | Water contact and landing g | clamp, landing number | M | medium | landing g, swell, hot landing |

Batches for the coding agent: 1 and 2 together (they calibrate against each other); 3 alone;
4 with the documentation pass.

## Interfaces that change

- `UKiteComponent`: `ProjectedAreaRatio`, `GetProjectedAreaM2()`, `SetRiderAirborne(bool)`,
  `AirborneZenithGain`, `AirborneZenithMaxHeadingDeg`, `AirborneTrimEaseDeg`; defaults of
  `ZeroLiftAngleDeg`, `MaxLiftCoefficient`, `StallAngleDeg`, the trims.
- `UBoardMovementComponent`: `EdgeReleaseSeconds` default 0; item 3 replaces the grip tunables
  with `bAutoEdge`, `MaxHeelDeg`, `LoadExtraHeelDeg`, `HeelResponse`, `FinAreaM2`,
  `RailAreaM2`, `LateralLiftSlopePerRad`, `LeewayStallDeg`, `TailWeightRailScale`,
  `LoadHoldBonus`, and `GetHeelDeg()`, `GetLeewayDeg()`; item 4 adds `LandingAbsorbDistanceCm`,
  `CrouchAbsorbBonus`, `CrashLandingG`, `HotLandingSinkMS`, `HotLandingKiteElevationDeg`,
  `WasLastLandingHot()`, and `OnBoardLanding` reports a real g.
- `AKiteRiderPawn`: passes the airborne state to the kite each step.
- Tests: the ride fixtures keep `bParkHoldAssist` on; new scenario tests as listed; re-based
  numbers listed per commit.
