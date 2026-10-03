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
| T3.4 PR 1 bar state machine (pure), T3.6 freestyle heat scoring | #65 | Merged; the heat scoring is used by the T3.6 heat flow below |
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
| Jump grade from the landing verdict (decision 6): `FJumpRecorderInput` carries the verdict's grade, `FJumpRecord::Grade` is set from it, the card shows the cause line for any named cause | #90 | Merged. The timed 30 kn jumps are now graded Sketchy (too hard) on the record and the card, where `GradeLanding` said Clean |
| T2.4 kite change: in-air loop entry. In the air a full bar (`AirLoopFullBarThreshold`, 0.85, at the kite after the dead time) held for `AirLoopHoldSeconds` (0.3 s) loops the kite its way from any clock, and a full bar reversed mid-loop and held starts a loop the other way (a keyboard tap still flies it across); a send held through the take-off counts only once eased or reversed; on the water the 35 deg rule is unchanged | #93 | Merged. S-loops and contra loops can now be flown and are named from a real jump ("S-loop", "Contra loop"); the contra sign is still not confirmed against footage |
| T2.1 grabs and T2.2 the one-footer: `IA_GrabFront` (LB, Q), `IA_GrabBack` (RB, E), `IA_OneFoot` (L3, C), in the air only; pure `FGrabState` (`Tricks/GrabState.h`: reach 0.2 s, hold from the reach, zone from the stick latched at the reach, one hand at a time, both buttons reserved for the board-off); the grab's tuck into the attitude; `FTrickGrab` entries, `bOneFooter` and `OneFootSeconds` in `FJumpRecord`, named and scored by the existing naming and scoring from 0.3 s; the pose (torso fold `TorsoPitchDeg`, `PelvisAnchor`, the drawn board pulled to the hand, `SolveArmsPerHand`); the back ankle out of its strap; `EFootStrapState` and the causes `FootOutOfStrap` (crash) and `FootLate` (sketchy) in the landing evaluator; `kitesurf.Trick`; `kitesurf.Shot Close`; the `AssetsValid` guard narrowed to "not bound to IA_Steer" (decision 7) | #97 | Merged. Tests `KiteSurf.Trick.GrabNamesByHandAndZone`, `GrabHoldCounts`, `GrabOnRide`, `OneFooterFootReturns`. Not in it: steer authority with a hand off the bar, through-the-legs grabs, a HUD hold timer, a grab sound |
| T2.3 the board-off: the chord of `IA_GrabFront` and `IA_GrabBack` in the air (no new action); pure `FBoardOffState` (`Tricks/BoardOffState.h`, owned and stepped by `FGrabState`: removal 0.25 s, variant from the stick latched at the hands, hold, re-catch 0.3 s whose last 0.12 s are a sketchy grace); the variants plain, superman, tic tac (360 about the long axis in 0.5 s) and board pass (round the back in 0.9 s); `BoardOffPose` (held board, grips, hip fold, ankles in the rider frame) for the drawn rider; `SetRiderBoardCatch` into the landing (`bBoardAttached`, new `bBoardCaughtLate` and cause `BoardCaughtLate`); `BoardOff` and `BoardOffSeconds` in `FJumpRecord` and the signature; per-variant technicality; the board's inertia approximated with the tuck | #102 | Merged. Tests `KiteSurf.Trick.BoardOffNotCaughtCrashes`, `BoardOffCaughtLands`, `BoardOffVariantsNamed`, `BoardOffFollowsHands`. Not in it: the "CATCH" HUD prompt, the board flip, steer authority with both hands off the bar, the board's inertia composed in the attitude |
| T3.1 PR 1 the kite side of unhooked freestyle: `UKiteComponent::SetLowParkAssist` (bar centred holds the kite at `LowParkElevationDeg` 45 at the window edge, on the water and in the air, ahead of the airborne overhead hold and `bParkHoldAssist`), `RequestFlick` (trim down `FlickTrimDropDeg` 8 and the low park `FlickClockDeg` 10 further round, for `FlickSeconds` 0.3), `SetLeashed` (no steering or assist, trim `LeashTrimDeg` -45, felt tension capped at `LeashTensionCapN` 120 N, no relaunch while leashed), `SetLeashAnchor` for the centre lines, `GetTrimOffsetDeg` | #103 | Merged. Tests `KiteSurf.Kite.UnhookedParksLow`, `LeashFlagsKite`, `FlickDropsTension`, `FreestyleAssistsOffByDefault` (in `FreestyleKiteTests.cpp`). Nothing calls them yet: the unhook, the sheet lock at the stopper and the leash on bar loss come with the pawn in T3.1 PR 2 |
| T3.1 PR 2 and PR 3 unhooked riding in the pawn: `IA_Hook` (Y, F) on the water and `IA_Pass` (X, LeftShift) in the air; `BarStateMachine` stepped after the kite (`AKiteRiderPawn::StepBar`); unhooked, the kite's sheet held at `UnhookedStopperSheet` and every bar input routed through `SheetKite` into the arm extension; the low park on; the line torque at the hands (`LineAttach::AttachPointBody`, `HandsLineTorqueScale` 1.0); the grip limit leashing the kite and crashing the rider (on the water at once, in the air at the touchdown with cause `BarLost` via `SetRiderBarInHands`); the flick on an air pass (`bFlickAssist`); the bar drawn at the hands, behind the back, on the pass arc or on the leash; HUD hook icon and grip meter; motion bar recentred on every hook toggle; a reset rehooks; passes into `FJumpRecord` and the signature (`SummariseJump`, `ApplySummaryToSignature`). PR 3 measured the pop and lowered `UnhookedStopperSheet` to 0.45 (low park kept at 45 deg): 1.86 m, 8h/t² 7.34 m/s² | #104 | Tests `KiteSurf.Trick.UnhookedPopTelemetry`, `UnhookedPopIsBallistic`, `HookedPopFloatsMore`, `UnhookedOverpowerLosesBar`, `UnhookedHoldsBelowLimit`, `HookOnlyOnWaterOnPawn` (the pure `HookOnlyOnWater` keeps its name), `ResetRehooks`, `BarLostInAirCrashesAtTouchdown`, `UnhookedArmExtensionRouting`, `LineAttachPoints`, `PassOnRide` ("Backside 1 to blind"; a backside 3 is not reachable with the T1 spin rates), `KiteSurf.MotionBar.UnhookRecentres`, `KiteSurf.Rider.UnhookedBarPose`; `KiteSurf.Input.AssetsValid` extended. Not in it: a FLICK ASSIST settings row, single-hand releases while unhooked (grabs keep both hands on the bar in the bar machine), the slack meter and hand dots on the HUD, the raley tracker (T3.2), stances (T3.5) |
| T3.2 the raley and the S-bend from the line torque, T3.3 flips and one hand off: `FRaleyRecognizer` (`Tricks/RaleyRecognizer.h`) stepped by the recorder (`FJumpRecord::bRaley`, `bSBend`, `SBendSense`, `MaxTiltDeg`, `LineSpinDeg`, `TakeoffMove`; `ETrickMove` moved to `TrickTypes.h` as a UENUM); the raley's arms (`AKiteRiderPawn::HasRaleyArms`, `RaleyArmExtension` 0.85) hold the landing assist off while the body swings out (`bAssistWaitsForRaleySwing`) and turn the roll pre-wind and stick about the lines (the S-bend); `HandsLineTorqueScale` 1.0 to 1.4 (1.0 gave 57 deg, the lines being slack for the first 0.5 s of the unhooked pop); `InertiaExtendedKgM2`; `HookedFlipScale` 0.35; the flip's arms in (`FlipArmExtension` 0.15); the tantrum's back hand (`bTantrumBackHandOff`, `RegrabBeforeContactSeconds` 0.4); unhooked grabs release that hand (`FBarInputs::bReleaseFront`/`bReleaseBack`) and the drawn hand goes to the board; a "Front flip" row in the freestyle table | #106 | Merged. Tests `KiteSurf.Trick.RaleyFromUnhookedPop` (62.4 deg, "Raley", sketchy), `HookedPopNoRaley`, `RaleyNeedsExtendedArms`, `RaleyStepRateIndependent`, `ClassifiesSBendSense`, `SBendFromRaleyRoll` (frontside "Hinterberger"; backside recognised but named "S-bend + backside 360 to toeside", a documented gap), `TantrumIsBackFlip`, `FrontFlipUnhooked`, `HookedFlipUnderRotates`, `OneHandAttachRolls`, `UnhookedGrabTakesHandOff`. Not in it: a dedicated raley rig pose (the attitude and the attach point draw it), an unhooked board-off with both hands off the bar, the flat-stick "plain hand-off" pose |
| T3.6 the freestyle heat flow: pure `FFreestyleHeat` (`Tricks/FreestyleHeat.h`: an attempt is an unhooked jump over 0.4 s of airtime or any crash, scored with `FreestyleTrickScore` from its record; hooked landings ignored with "Unhook for freestyle"; the 90 s trick countdown per attempt, optional; the attempt limit), `UFreestyleHeatSubsystem` polling the tracker (refuses during a session or lesson, cancelled when one starts), HUD row "Trick 3/7" with the countdown and the total with the variety bonus, the counting list with families, the results card, local best per attempt count in the save game (`BestFreestyleHeatTotalByAttempts`), `kitesurf.Heat freestyle [attempts] [countdown s]`, pause menu "FREESTYLE HEAT (7 tricks)" (free ride only, after SCHOOL) | (this PR) | Tests `KiteSurf.Trick.FreestyleHeatAttempts`, `FreestyleHeatCountdown`, `FreestyleHeatOnPawn`, `FreestyleHeatHUD`, `FreestyleHeatStartsFromPauseMenu`, `FreestyleHeatBestPersists`, `FreestyleHeatOldSaveLoads`. A world subsystem like the session's, not `AKiteSurfGameMode::StartFreestyleHeat` as the T3 plan sketched (test worlds have no game mode). |
| T3.5 landing stances: `ETrickStance` on the pawn (decision 2; `GetRidingStance`, `GetStanceSeconds`), stepped after the board (`StepStance`) from the bar's effective wrap and route at an unhooked touchdown (and through the 0.2 s the body settles on its rail); Heelside whenever hooked, without the bar, crashing or floating; Toeside held `ToesideHoldSeconds` (3.0) and Blind `BlindHoldSeconds` (2.0), then the slide round (`StartUnwindingStanceTurn`); X on the water: Toeside the half turn back, Blind the bar machine's surface pass (`SurfacePassMaxTensionBW` 0.6, `SurfacePassSeconds` 0.4, `SurfacePassLoseTensionBW` 0.9; joins the jump within `SurfacePassGraceSeconds` of the touchdown, `MaySurfacePassJoinJump`, the recorder waiting for it) then the backside half turn, Blind already passed the half turn once the tension allows; the straps' nose side kept for the bar while a stance is held; `FRiderRigInput::TorsoTwistDeg` and `FRiderRigPose::Hips` (legs on the hips, torso and arms twisted), `ToesideTorsoTwistDeg` 70; the stance in `kitesurf.State` | (this PR) | Tests `KiteSurf.Trick.LandsBlindAfterBackToBlind`, `ToesideLandingHeld`, `StanceTimesOutToHeelside` (60 and 30 fps), `SurfacePassFromBlindCounts` ("Backside 1 to blind" with a surface pass), `KiteSurf.Rider.BlindPoseBarBehindBack`, `ToesideTorsoTwisted`, `HookedBackToKiteStillSlidesRound` (new: the check in `SpinsWithBoard` as its own test); `PassOnRide` rides away for the blind hold plus 1 s. The ride-away retention per grade (`SpeedRetention*` in the landing evaluator) was already in. From toeside X turns back the way the frontside 180 came (it unwinds the lines; a further frontside 180 would wrap them), not a frontside 180 on. Not in it: hooked toeside riding (hooked stays heelside), a HUD stance cue, riding on and taking off toeside or blind (T3.7) |
