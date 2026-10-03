# Trick implementation plans

Code-level plans for the milestones in `docs/tricks.md`, one file per milestone:

- `T0.md`: groundwork (fixtures, loop records, jump record, naming, scoring, jump card)
- `T1.md`: first rotations (visual board, rider attitude, rig quaternion, landing evaluator, input, live tracker)
- `T2.md`: big air depth (grabs, one-footer, board-off, loop families, session, failure causes, trick book)
- `T3.md`: freestyle (unhooked, raley, flips, handle pass, stances, heat) and an outline of T4 (strapless)

Each plan was written by a planning agent that read the code on 2026-10-03. Line numbers are
against `origin/main` at `13f8e13` or `10a5d0a`, and `main` has moved since. Find code by function
name and use the line numbers only as hints. **Where a plan disagrees with this README, this
README wins.**

## Shared decisions

These reconcile the four plans.

1. **Where files go.** All new trick code goes under `Source/KiteSurf/Public/Tricks/` and
   `Private/Tricks/` and is included as `"Tricks/…"`. This covers T1's
   `LandingEvaluator.h` and `RiderAxes.h`, T3's `BarState.h`, and T0's `KiteLoopTracker.h`.
   Tests go in one file per PR (`Private/Tests/Trick<Area>Tests.cpp`), so that parallel PRs do
   not conflict on a single `TrickTests.cpp`.
2. **One set of enums**, in `Tricks/TrickTypes.h`:
   - `ELandingGrade { Stomped, Clean, Sketchy, Crash }` and `ELandingCause` (T1's list, plus T3's
     `BarLost` and `PassUnfinished`, plus `BoardNotAligned` for T4). These replace T0's
     `ETrickLandingGrade`.
   - `ETrickStance { Heelside, Toeside, Blind }`. This replaces T3's `ERidingStance`, and the
     pawn uses it as well.
   - `ETrickSense { None, Frontside, Backside }`. This replaces T3's `int8 Sense`.
   - The inversion, loop, grab, board-off and pass enums are as in T0 section 5.
3. **Freestyle naming has one table.** T0 owns `TrickNaming` and its freestyle table. T3.4 extends
   that table with the GKA family and difficulty columns from T3 section 4. There are not two
   tables.
4. **Back to blind** is a backside spin with a Blind landing and no pass (T0 section 9). This
   agrees with T3's wrap model.
5. **The tracker** is `UTrickTrackerComponent`. It polls counters, because delegates are not
   bound in tests. It is stepped from `AKiteRiderPawn::StepSimulation`, after the board. The
   rider attitude is stepped before `StepBoard` (T1 section 2.10).
6. **Landing g.** `LandingAbsorbDistanceCm` (30) and `ComputeLandingG` keep the names from physics
   phase 2 item 4. T1.5's evaluator owns the grade. Phase 2 only supplies the inputs.
7. **Input.** In the air, the left stick and WASD are read by state (pre-wind while loading,
   rotation in the air). There is no new `IA_Rotate`. LeftShift and RB are guarded by
   `KiteSurfHUDTests.cpp`: that guard is narrowed to "not bound to IA_Steer" in the same PR that
   first binds them.

8. **Rider rotation goes before the board.** The attitude step runs before `StepBoard` (T1),
   not after it (T2 addendum).
9. **Every rider can do tricks.** Since #54 the robot also uses the jointed rig, so the robot
   question in `docs/tricks.md` sections 6.5 and 9 is closed.
10. **Each plan ends with an addendum** that re-bases it on `a50c315`. Use the addendum's line
    numbers.

## Order against physics phase 2

Physics phase 2 (`physics/phase2`, `docs/physics/plan-2.md`) was being written on 2026-10-03. It
touches:

- `BoardMovementComponent`, `KiteComponent` and the step in `KiteRiderPawn`;
- `RideLoopTests.cpp`, `BoardMovementTests.cpp` and `PhysicsScenarioTests.cpp`.

Until phase 2 merges, trick PRs avoid those files. They ship as pure modules with their own
tests, and the wiring comes after phase 2.

| Can go now (new files, pure) | Waits for phase 2 |
| --- | --- |
| T0.4 types, naming, scoring, `FJumpRecord` structs | T0.1 fixture move (conflicts with phase 2 test edits) |
| T0.3 `FKiteLoopTracker` (pure), T2.4 loop classifier | T0.3 hookup in `KiteComponent` |
| T0.2 `FJumpRecorder` (pure) | T0.2 board events, landing g, tracker in the pawn |
| T1.5 `LandingEvaluator::Evaluate` (pure) | T1.5 wiring into the board's landing |
| T1.2 `URiderAttitudeComponent` pure step (PR D) | T1.1 visual board, T1.2 wiring (PR E), T1.4 input, T1.6 live tracker |
| T1.3 rig takes the body quaternion (identical behaviour on the water) | |
| T3.4 `BarStateMachine` (pure), T3.6 freestyle heat scoring | T3.1 and later (kite assist, pawn) |
| T2.7 trick book in the save game | T2.1 to T2.3 (need T1 wiring) |

## Status

Updated as PRs merge.

| Item | PR | State |
| --- | --- | --- |
