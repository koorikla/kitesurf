# Physics plan, phase 3

Two items, in this order: a bug the user hit, and the descent gap phase 2 left open. Same ground
rules as `plan-2.md`; numbers reported against `main` at `b7f9ac6` (phase 2).

## 1. A crashed or floating rider is flung by the kite

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

## Order

Item 1 first (the bug), then item 2. One commit per step, built and tested green. Then merge
with `main`, PR, CI, squash-merge.
