# Physics plan, phase 3

Two items, in this order: a bug the user hit, and the descent gap phase 2 left open; a third, the
board over-rotating under the kite, came later from the user. Same ground rules as `plan-2.md`;
numbers reported against `main` at `b7f9ac6` (phase 2).

## 1. A crashed or floating rider is flung by the kite

**Status: done with deviations** (`b4f95e6`, `035c6df`). The 30 m throw was the board's crash, not the
floating rider: the crash branch never spent the line force, and 1.5 s of it came out in one step after
the reset (`Physics.CrashedRiderIsNotFlung`). The floating changes are in as designed. The looped-kite
target holds with the bar let go (3.7 m/s, 7.5 m in 25 kn); with the bar in the loop is a downloop water
start that rides the rider away (8.0 m/s, 21 m), so the 15 m is not asserted for it. The snatch was not a
cause: no line compliance. Re-based: `Gear.ChangesBehaviour` (the small board's depth) and
`Physics.FloatingRiderIsSlowThroughTheWater` (the drift without the drag, as a ratio).

**Report.** After a crash, with the rider in the water and the kite in a bad spot (deep in the
window, or relaunching), the rider is thrown about 30 m.

**Suspects** (`BoardMovementComponent.cpp`, `KiteComponent.cpp`):
- The floating rider's water drag fades to nothing above `FloatingDragFadeSpeedCmS` (2 m/s),
  added in phase 2 item 3d so that water starts still worked. Once the kite gets a floating
  body moving, nothing holds it back. A body dragged through water keeps its drag; what changes
  is that the pull lifts it onto the surface.
- The pull on the rider is capped only at `MaxLineTensionN` (6 kN, seven body weights), in the
  water as in the air.
- The float depth depends on speed alone (`GetFloatDepthForSpeed`), so a rider cannot rise
  until they are moving and cannot move until they rise; the fade was the way round that.
- Slack lines snatching tight while the rider is in the water (the kite's outward velocity is
  removed on the kite's side; the rider feels that step's tension).

**Change.**
- Reproduce first, with the debug CSV: the rider floating in 20 and 25 kn while (a) the kite is
  looped through the power zone, (b) the kite crashes and relaunches, (c) slack lines snatch
  tight; and the crash state followed by its reset. Record the rider's peak speed and
  displacement over 5 s and the step where it goes wrong. Put the trace summary in the commit.
- Remove the speed fade. The floating drag stays while the body is in the water, at every
  speed; it goes away only as the body rises. Make the float depth respond to the pull as well
  as the speed: the target depth falls with the line tension through `FloatRiseTensionN`
  (about 0.6 body weights: a strong pull lifts a rider onto the board before they are moving),
  so a water start is the kite lifting the rider, then the board planing. Keep
  `FloatingDragAreaM2` in its research range (0.3 to 0.5 m^2 for a body and a sunk board).
- Bound the pull a rider in the water can take: a submerged rider's lines pull through a body
  with a drag ceiling, so the speed the kite can drag them at is bounded by the drag; no
  separate cap is needed if the drag is right. If the trace shows a single-step snatch impulse
  doing the damage, spread it with the line's compliance (phase 1 item 11: `LineStiffnessNPerM`
  8 to 12 kN/m as an XPBD compliance on the existing constraint) rather than clamping.
- Targets: a floating rider dragged by a kite looped through the power zone in 25 kn never
  exceeds 4 m/s through the water and moves under 15 m in 5 s; a water start in 20 kn with the
  bar in and the kite dived still planes within 10 s; `Ride.FloatsUntilPlaning`,
  `Physics.FloatingRiderIsSlowThroughTheWater` and the gear water-start comparison still pass.
- Tests: `KiteSurf.Physics.FloatingRiderIsNotFlung`, `KiteSurf.Physics.WaterStartIsTheKiteLiftingTheRider`,
  and the snatch case if it was a cause.

## 2. The descent: redirect before landing, and the kite kept flying

**Status: not done; measured** (`CHANGELOG.md`, Phase 3). No design of the redirect met the targets
together: the best sink was 5.6 m/s with the kite at 53 to 57 deg at touchdown, 7.2 m/s with it above 60;
the ride-away was always slower than the touchdown; storm landings got harder. The kite stalls in the
redirect's turn (21 to 23 deg), not in the hold; easing the trim keeps it flying but shrinks the burst.
Nothing of it is in the code.

**Problem** (phase 2 changelog). The timed 30 kn jump comes down at 8.2 m/s with the kite at
55 deg; real descents are 3 to 6 m/s. In the last two seconds the sink speed tilts the apparent
wind the kite sees, the window's top moves downwind, and a kite held at 12 loses elevation and
airspeed. Storm jumps come down at 12 to 13 m/s and crash.

**What riders do** (research 3.3): the redirect. One to two seconds before touchdown the kite
is flown forward in the direction of travel. Its own speed adds to its airspeed, the pull
rises and turns forward and up, the sink slows, and the rider lands moving forward.

**Change.**
- `bAutoRedirectAssist` on the kite (default true; the design doc lists auto-redirect as a
  sim-cade assist with its own toggle). In the air with the bar centred, when the rider is
  below `RedirectStartHeightCm` (500) on the way down or sinking faster than
  `RedirectSinkMS` (5), the assist steers the kite from 12 forward and down on the travel side
  (towards the window's edge in the direction the rider is moving) with `RedirectGain` and up
  to `RedirectHeadingDeg` (60), and back towards 12 as the sink eases. The player's own bar
  still overrides (bar over travels, towards the kite loops). `AirborneZenithGain` 0 and the
  assist off together give the phase 2 air.
- Keep the kite flying through the descent: with the sink speed in the apparent wind the angle
  of attack rises; the trace will say whether the kite stalls or merely loses elevation. If it
  stalls, the airborne assist may ease the trim target by up to `AirborneTrimEaseDeg` (plan-2
  A4, still a knob).
- Targets (`Physics.GoodLandingIsThreeToSixG`, `Physics.HangTime`, a new
  `Physics.RedirectSoftensTheLanding`): the timed 30 kn jump touches down sinking 6 m/s or
  less with the kite above 60 deg, lands at 2 to 5 g crouched, and rides away faster than it
  touched down; `8h/t^2` stays 1.5 to 5 and the height 10 to 20 m. Storm (`Wind.StormIsRideable`):
  the 60 kn best jump lands (hot is fine) rather than crashing; 90 kn is reported, not
  required. `KiteOverheadInTheAir` keeps its 50 deg floor until the redirect starts.
- Document in the changelog what the redirect changed against `b7f9ac6`.


## 3. The harness limits how far the board can point from the pull

**Status: done with deviations** (`42b9495`, `0ddcdfa`). The limit, the return, the lean and the tests
are in as designed, with 50 deg kept. Not designed and needed: the carve's lean is now the one the
turn needs (`v w / g`), not `CarveHeelDeg` times the input at any speed, which on a nearly stopped
board drove it round through the wind; and the low-speed pivot does not take a nose past the limit.
Not met: "`HarnessLeanAmount` reaches 1" with the stick held. The board stalls about 15 deg past the
pull's beam and gets no more than 22 to 25 deg past it in 15 to 30 kn, so the limit (40, 50 or 60) is
never reached there and the lean does not build; it is tested where the harness turns a board back.
None of the carve, upwind and course tests points past the limit, so none is re-based; the drop-off
test does not ride at it (at least 10.5 deg short of the pull's beam). Re-based instead:
`Physics.FloatingRiderIsNotFlung` (the lifted rider's board now turns nose first to the pull).

**Report (user, 2026-10-03).** Riding on the left tack and holding the stick fully left, the
board keeps turning and the rider ends up rotating round under the kite in the water. A rider
cannot rotate about the lines like that: the harness hook is on the front of the waist and the
feet are in the straps, so the board can only point so far upwind of the pull before the body
cannot twist further. Past that, more stick should be lean against the harness, and pointing
that high bleeds speed until the rider stalls off the plane, with no rotation.

**Today.** The carve input yaws the board kinematically at `CarveTurnRate` (60 deg/s at full
input) with no reference to the lines (`BoardMovementComponent.cpp`, the carve branch in
`StepBoard`), so holding the stick turns the board through any angle. The pawn's
"back to the kite, slide round after 0.4 s" logic then spins the rider's stance.

**Change** (in the board's step, on the water, lines taut; nothing changes in the air):
- The pull direction `P` is the horizontal unit vector of the line force. The beam-reach
  heading on the current tack is perpendicular to `P` on the side the board is travelling; the
  heading's angle upwind of it, `UpwindOfBeamDeg`, is signed so that turning away from the kite
  raises it. The same rule on both tacks and in both stances (heelside and toeside), so a kite
  flown to the other side does not yank the board round: it only moves the beam heading.
- `MaxUpwindHeadingDeg` (default 50; tune 40 to 60): the carve input cannot yaw the heading past
  it. The requested yaw beyond the limit becomes harness lean instead: `HarnessLeanAmount`
  (0 to 1) builds at `HarnessLeanRatePerS` (1.0) while the stick is held against the limit and
  decays at `HarnessLeanReleaseRatePerS` (3.0) when it is not. The lean adds heel towards the
  kite, `HarnessLeanHeelDeg` (20) times the amount, into the force balance (like loading), so
  the hump pressure drag and the heel slow the board; the rider mesh leans back with it
  (expose `GetHarnessLeanAmount()` for the pose and the debug text).
- If the heading is outside the limit because the pull moved (the kite was flown somewhere
  else), the harness turns the board back inside it at `HarnessYawRateDegPerS` (90), on the
  water with taut lines only. With slack lines, floating, crashing or airborne there is no
  limit, as now.
- Carving towards the kite (downwind) is unchanged, including riding tail-first and the
  twin-tip's switch of ends.

**Targets and tests.**
- `KiteSurf.Physics.HarnessStopsOverRotation`: riding on the left tack in 15 and 20 kn with the
  stick held fully left for 6 s, the board's heading never passes `MaxUpwindHeadingDeg` upwind
  of the beam reach, the rider's stance side never flips, `HarnessLeanAmount` reaches 1, and the
  board slows (and may drop off the plane) instead of turning; releasing the stick lets the
  heading come back and the rider ride on. Mirrored on the right tack.
- `KiteSurf.Physics.HarnessBringsTheBoardBack`: with the board pointed 70 deg upwind of the
  beam by hand on the water, taut lines bring it back inside the limit within 1 s.
- `Ride.CarveIsSymmetric`, `Physics.CarveFollowsTheHeading`, `Movement.UpwindAngle`,
  `Physics.UpwindAtEdgeAngle`, `Physics.CourseTheorem` and `Physics.PointingTooHighDropsOffThePlane`
  re-based to the limit where they pointed higher than it (the drop-off test rides at the
  limit); `Rider.SpinsWithBoard` keeps its hand-posed spin in the air; the in-water
  "slide round to face the kite" in the pawn keeps working for the stance.
- Record in the changelog what moved against `b7f9ac6`.

## Order

Item 1 first (the bug), then item 2, then item 3. One commit per step, built and tested green. Then
merge with `main`, PR, CI, squash-merge.
