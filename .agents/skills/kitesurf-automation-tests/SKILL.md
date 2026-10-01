---
name: kitesurf-automation-tests
description: Write Unreal automation tests for KiteSurf physics and gameplay.
version: 0.1.0
metadata:
  hermes:
    tags: [kitesurf, unreal-engine, testing, physics, automation]
    related_skills: [kitesurf-build-test, kitesurf-big-air-sim, physics-tuning]
---

# KiteSurf automation tests

How tests are written in this repository, and what a physics test has to prove before a
backlog task's acceptance criterion counts as met.

## When to Use

- Adding or changing anything under `Source/KiteSurf/`.
- Turning an acceptance criterion from `docs/research.md` into an executable check.
- Reviewing a change that claims a physics result (jump height, line tension, hangtime).

To run tests, use `kitesurf-build-test`.

## Procedure

1. **Put the test in `Source/KiteSurf/Private/Tests/<Area>Tests.cpp`**, wrapped in
   `#if WITH_DEV_AUTOMATION_TESTS` ... `#endif`.
2. **Name it `KiteSurf.<Area>.<Behaviour>`.** The runner executes
   `Automation RunTests KiteSurf`, so a name without that prefix never runs.
   ```cpp
   IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKiteSurfBoardSettlesAtRest,
       "KiteSurf.Board.SettlesAtRest",
       EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

   bool FKiteSurfBoardSettlesAtRest::RunTest(const FString& Parameters)
   {
       UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
       TestNotNull(TEXT("World created"), World);
       // spawn, step, assert ...
       World->DestroyWorld(false);
       return true;
   }
   ```
3. **Step the simulation by hand with a fixed step.** Existing tests create a bare world,
   spawn `AKiteRiderPawn`, and call `Tick` / `TickComponent` in a loop with a constant
   `DeltaTime`. There is no water body in that world, so the water surface is flat at Z = 0.
4. **Assert numbers with tolerances** using `TestNearlyEqual(What, Actual, Expected,
   Tolerance)`. Put the unit in the message (`cm`, `cm/s`, `N`, `deg`).
5. **Flag tests that need a renderer** with `| EAutomationTestFlags::NonNullRHI`. CI runs
   with `-nullrhi`, and those tests belong in a separate GPU job.
6. **For physics changes, add three kinds of check:**
   - a *target* check against the range in `docs/research.md` (for example zenith line
     tension of 0.9 to 1.0 kN for a 9 m² kite at 30 kn);
   - *invariants*: no NaN, line length never above its limit, energy bounded;
   - *step-rate independence*: the same scenario at 60, 120 and 240 Hz agrees within a
     stated tolerance.
7. **Run the suite** and confirm the test count went up by the number of tests you added.

## Pitfalls

- **Placeholder assertions.** `TestTrue(TEXT("..."), true)` proves nothing; one such test
  exists in `KiteRiderPawnTests.cpp` and should not be copied.
- **Weak inequalities that hide a bug.** A "monotonic" check written with `<=` passes when
  the value is flat. Use strict comparisons where the behaviour must actually change.
- **Testing components only in isolation.** A kite test and a board test both passing says
  nothing about the coupled system. Jumps, loops and landings need the pawn stepped as a
  whole.
- **Units.** Unreal positions are centimetres and forces in this code are kg·cm/s²
  (1 N = 100). Convert at the boundary and say which unit an assertion uses.
- **Frame-rate dependent results.** If a result changes with the step size, the test should
  fail, not have its tolerance widened.
- **Forgetting `DestroyWorld`.** Leaked worlds make later tests flaky.

## Verification

- The new tests appear in the run output and `Total` increased accordingly.
- Each acceptance criterion in the task maps to at least one named test, listed in the
  change description.
