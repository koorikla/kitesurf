---
name: kitesurf-big-air-sim
description: Implement KiteSurf kite, board and jump physics from the research.
version: 0.1.0
metadata:
  hermes:
    tags: [kitesurf, physics, simulation, big-air, game-design]
    related_skills: [kitesurf-automation-tests, physics-tuning, game-feel, camera-systems]
---

# KiteSurf big air simulation

The rules for changing the kite, board, wind or jump model. The evidence, formulas and
parameter values are in `docs/research.md`; this skill says how to apply them.

## When to Use

- Any task in epics A, B or C of the candidate backlog in `docs/research.md`.
- Any change to `UKiteComponent`, `UBoardMovementComponent`, `UWindComponent` or
  `AKiteRiderPawn`.
- Tuning jump height, hangtime, kite loops, line tension or landings.

## Procedure

1. **Read the matching section of `docs/research.md` first.** Use its parameter tables for
   defaults. Each number is tagged *sourced*, *typical* or *estimate*; treat estimates as
   starting points for tuning, and do not present them as facts.
2. **Keep the model honest where skill comes from.** Height must come from the send and
   the edge release, with the kite doing work during the climb. Do not add a scripted jump
   impulse.
3. **Never drop these four things:**
   - the kite's own velocity in its apparent wind (`v_a = v_wind - v_kite`);
   - a slack-line state (tension is never negative);
   - the coupling between depower and steering response;
   - steering dead time.
4. **Build on a fixed step.** The target design (backlog task A2) is a constant step of
   240 Hz, semi-implicit Euler, and a distance constraint for the lines that is active only
   when taut, with rendering interpolating between steps. Today the components still
   integrate at the variable frame step; do not add new code that depends on frame time.
5. **Units.** Unreal uses centimetres; do the aerodynamics and hydrodynamics in SI inside
   the simulation and convert once at the boundary. One knot is 0.5144 m/s. Define each
   constant once.
6. **Put tunable values in data**, with units in the property name or tooltip, not as
   literals in the force code.
7. **Calibrate against the checks in the research:**
   - a parked kite sits about `atan(L/D)` from the downwind axis;
   - zenith tension for 9 m² at 30 kn is about 0.9 to 1.0 kN sheeted in;
   - full-bar loop radius is about `1 / g_k`, independent of speed;
   - effective gravity during a jump, `8h / t^2`, is 1.5 to 3.5 m/s²;
   - a megaloop peaks at 3 to 5 body weights with the kite low.
8. **Write the tests with the change** (`kitesurf-automation-tests`): the target range,
   the invariants, and step-rate independence.
9. **Expose telemetry** (height above local water, airtime, peak tension, landing g) so
   tuning is done from numbers.

## Pitfalls

- **Power zone weaker than zenith.** If the kite pulls hardest overhead, the apparent wind
  is missing the kite's own motion. This is the current state of the code.
- **Water forces while airborne.** Planing lift, drag and carve must be gated on contact.
- **One axis doing three jobs.** Edge angle, grip and heading are separate; a rider must be
  able to hold an edge against the kite without turning.
- **A stiff spring for the lines.** Real line stiffness makes explicit integration blow up
  at game step sizes. Use a constraint.
- **Tuning to a record.** Jump-sensor heights over-read by 20% or more, and the highest
  recorded jumps were set in 40 kn with specialist gear. Tune the ordinary cases first:
  a 10 to 20 m jump in 30 kn.
- **Realism over readability.** Where the research lists a place that realism should yield
  (upwind drift, lulls, loop catch by default), follow it; assists are separate toggles and
  must not be baked into the physics.

## Verification

- The acceptance criterion from the backlog row is quoted in the change description with
  the measured value next to it.
- The calibration checks above that the change touches are covered by tests.
- Results are unchanged, within tolerance, at 60, 120 and 240 Hz.
