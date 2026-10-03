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
   Since #90 the evaluator's verdict is also the only source of a jump's grade:
   the tracker feeds the board's `GetLastLandingVerdict()` grade to the recorder, which sets
   `FJumpRecord::Grade` from it (`TrickScoring::GradeFromVerdict`; a crashed jump stays Crash), so
   the score's execution and the trick card follow the verdict, and the card shows the verdict's
   cause line whenever it names one. `TrickScoring::GradeLanding` remains as the record-only
   shortcut for records built without a board (pure tests).
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
| T0.4 trick core: types, signature, naming, scoring | #60 | Merged |
| T1.2 PR D rider attitude pure step | #61 | Merged; stepped by the pawn since T1.2 PR E |
| T1.5 landing evaluator (pure) | #62 | Merged; grades the board's landings since T1.2 PR E |
| T0.3 kite loop tracker (pure), T2.4 loop classifier | #63 | Merged |
| T0.2 jump recorder (pure), landing g helper | #64 | Merged |
| T3.4 PR 1 bar state machine (pure), T3.6 freestyle heat scoring | #65 | Merged (not used in the game) |
| T1.3 rig takes the body quaternion | #66 | Merged |
| T2.7 trick book in the save game | #67 | Merged |
| T1.1a visual board split from the physics root, air camera | #68 | Merged |
| T0.2/T0.3/T0.5 light wiring: `UTrickTrackerComponent` on the pawn, HUD trick card and ticker, `kitesurf.Jumps` | #69 | Merged. Polled public getters; take-off, popped, apex time, landing yaw and the loop turn were synthesised in the tracker |
| T0.2 board events (`BeginAirborne(bool)`, `OnBoardTakeoff`, `OnBoardApex`, take-off and apex counters, landing angle), T0.3 kite hookup (`FKiteLoopTracker` stepped by the kite, `GetLoopRecords`), tracker fed from them | #73 | Merged. The tracker derives nothing itself any more |
| T2.0 rig hand targets (`FRiderRigInput::Hands`, `SolveArmsPerHand`), board grab points (`Tricks/BoardGrabPoints.h`), strap loops moved to ±30 cm in `SM_KiteBoard` | #76 | Merged. Not used by the pawn yet (T2.1 grabs will); torso fold, tuck, pelvis anchor and `SolveGrab` from the T2.0 plan are not in it |
| T1.2 PR E rotation live: attitude stepped before the board, board air orientation and drawn board from it, rig body quaternion with a hand-over, travel align, `LandingEvaluator` wired into the board's landing (T1.5 wiring), scripted pre-wind, air stick and tuck | #79 | Merged. The tracker does not count inversions yet (T1.6) and grades from its own record, not the board's verdict |
| T2.5 best-three session: pure `FBestThreeSession` (`Tricks/SessionScoring.h`), `UTrickSessionSubsystem` polling the tracker, HUD clock row and results card, local best per length in the save game, `kitesurf.Session [seconds]`, pause menu entry | #84 | Merged. A world subsystem as the T2.5 plan says; best paid landing per family key, best three of those (F5 and GKA reconciled as in the plan) |
| T1.4 player input: the left stick / WASD read by state (board, pre-wind with the board latched, air rotation by the screen side of the rider's back), jump held in the air is the tuck, stick X alone with no pre-wind is a flat spin (`AirStickTiltWithoutPreWindDeg`), legend rows, `kitesurf.Stick` and `kitesurf.JumpButton` | #87 | Merged. No new input action or `.uasset` (decision 7) |
| T1.6 live rotation recognition: pure `FRotationRecognizer` (`Tricks/RotationRecognizer.h`) run by the recorder from the attitude (inversions, spin half turns and sense, landing stance, roll start), rotation fields in `FJumpRecord`, `SignatureFromJump` reads them and times early or late rolls in loops, live ticker; T2.6 cause line on the trick card from the board's verdict | #88 | Merged. Spin and net heading are measured against the flight's own turn; with an inversion the spin is the net heading's (0 or one half turn). The card's grade was still the record's `GradeLanding`, so the cause line showed only on sketchy or crashed cards (fixed by the next row). `RecognisesMegaloopBackRoll` is the synthetic `NamesMegaloopBackRollFromRecord`; the ride version waits for a real megaloop |
| Jump grade from the landing verdict (decision 6): `FJumpRecorderInput` carries the verdict's grade, `FJumpRecord::Grade` is set from it, the card shows the cause line for any named cause | this PR | Open. The timed 30 kn jumps are now graded Sketchy (too hard) on the record and the card, where `GradeLanding` said Clean |
