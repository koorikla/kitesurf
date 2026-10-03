# Interactive tutorials: kite school

Written 2026-10-03. This is a plan: nothing in it is implemented yet. It sets out a lesson
progression and a design for teaching it in the game, from the first water start to a 313.

The order follows the Kitesurf College channel's lesson playlists, cross-checked against the IKO
and VDWS levels and coaching sites. The teaching mechanics come from action-sports games that
teach well. Trick definitions and scoring are in `docs/tricks.md`, and the code-level trick plans
are in `docs/tricks/`.

Confidence tags as in `docs/research.md`. Every pass threshold tagged **estimate** is a tuning
starting point, not a requirement.

## 1. What the real progression looks like

### Kitesurf College

The [channel](https://www.youtube.com/channel/UC2AvhQhxmgRpM32IYE5qkOA) keeps a master playlist,
[Kitesurf lessons in order](https://www.youtube.com/playlist?list=PL41dAinz_9ZeaG5FJ_XU8vyBAE_HL_Cgl)
(109 videos on 2026-10-03). It also has ordered sub-series:

- [beginner's series](https://www.youtube.com/playlist?list=PL41dAinz_9Zd54PL1rxKlvNp-aRVWyCyE)
- [Learn to Jump](https://www.youtube.com/playlist?list=PL41dAinz_9Zd4m-hi1u_XbltOTc31ui7j)
- [Learn to Rotate](https://www.youtube.com/playlist?list=PL41dAinz_9Zc3DpJsRqf4Af_0jmjz6fbg)
- [First 14 Twintip Tricks](https://www.youtube.com/playlist?list=PL41dAinz_9ZfZn0X4T7dzBD_5eeRbtv1-)
- [Off the hook](https://www.youtube.com/playlist?list=PL41dAinz_9Zenw255roemm7GAFeVRl56E) (unhooked)

There is no app or levelled course; the playlists are the curriculum.

The master list runs through these modules, in order:

1. Kite control and the wind window.
2. Set-up, body drag, relaunch and water start.
3. Independent riding: upwind, transitions, stalls, light wind.
4. Toeside and carving.
5. Jumping: pop, then small and medium jumps, then jump transitions, then grabs.
6. First rotations: back roll, front roll, downloop, blind.
7. Gusts and safety.
8. Roll transitions, self launch and self land.
9. Big air: jumping higher, landing, heli loops, the kite loop introduction, the send.
10. Starts and stops.
11. Old school: grabs, board-offs, roll variations.
12. Slides and surface tricks.
13. Inverted pops.
14. Kite loops: heli to megaloop, then back roll kite loops.
15. Strapless.
16. Unhooked: raley, unhooked rolls, the surface pass, then the 313.

What the channel teaches about teaching (paraphrased from the lessons' captions):

- **Light wind first.** Every skill is learned in light wind; the wind speed is the difficulty
  dial. Kite loops are learned in 15 to 20 kn, not 25 to 30.
- **Tiny, then medium, then big.** Each movement is drilled on its own before the parts are
  combined. The pop is a carve plus a stomp; the water start is a kite dive plus the body.
- **Every lesson has a mistakes chapter**, and each mistake has a cause and a fix: dived early,
  dived late, front stall, edge lost.
- **Kite control is the safety anchor.** When a rotation goes wrong, keep the bar straight and the
  kite at 12, and steer only when the landing is in sight.
- **Gates.**
  - Upwind needs confident starts and stops on both sides.
  - Going higher needs consistent small-jump landings, so landing comes before height.
  - Heli-loop landings come before powered loops.
  - Unhooking needs jumps, rotations, basic kite loops and plenty of tricky relaunches.
- **A useful checkpoint.** Powered loops into heli loops without hard landings mean the kite loop
  fundamentals are there.
- **A height claim.** Above 10 m is reachable underpowered (18 to 22 kn) with good technique.
- **Pop before the kite jump.** The channel teaches the pop (a jump without raising the kite)
  first.
- **Back rolls before front rolls**, because most people find them easier.
- **A two-movement landing.** A back-hand steer about 4 m up, then a front-hand dive across 12 at
  1 to 2 m.

### Cross-check

- **IKO:**
  - Level 1, Discovery: kite control.
  - Level 2, Intermediate: body drag and water start.
  - Level 3, Independent: edging, upwind, transitions, toeside, self launch.
  - Level 4, Advanced: basic jump, power jibe, jump with grab, jump transition.
- **VDWS licence:**
  - L3: water start and 50 m downwind;
  - L5: upwind, which makes a rider independent;
  - L6: basic jumps;
  - L7: rotation or kite loop, grabs, one foot, board-off, raley.
- **Agreement:** Kitesurf College matches both for riding, and goes further and more carefully for
  tricks.
- **Where coaches disagree:** some schools teach the raley before kite loops.

The game follows Kitesurf College, which gates unhooking behind loops. The game is about big
air, and its freestyle comes later.

### What games do well, and badly

| Game | Pattern to copy |
| --- | --- |
| Trials Rising, University of Trials | Short lessons with a narrator, slow motion, input meters, an instructor ghost riding alongside, and context tips on a button. Closest model |
| Trials Evolution and Fusion | Licence tests and the next tracks unlocked by medals |
| Skate 3, skate.School | Small drill courses in Basic, Intermediate and Advanced tiers |
| Riders Republic, Tricks Academy | Pick a trick, see it, then practise straight in the open world. An assist ladder with a score incentive for turning assists off |
| OlliOlli World | A timing grade at the moment of the action (Perfect, Good, Sloppy). One new mechanic per level, spread over the game |
| Steep | Follow-the-rider demonstrations |

Mistakes to avoid:

- **Long chained tutorials that delay freedom.** Early-access skate. was criticised for this.
- **Bails with no reason given.** A reviewer of THPS 1+2 named this.
- **Text walls.** Following Celia Hodent's onboarding principles: learn by doing; give the purpose
  before the procedure; at most three new things at a time; spaced repetition; no early
  punishment; an input glyph plus a few words, shown only while that action is pending.

The kite games on Steam have a training mode at most (Winds Up) or nothing structured
(Kiteboarding Pro). A real kite school is an open gap.

## 2. The curriculum

Six chapters and 28 lessons. Each lesson has a prerequisite, a drill set-up, a pass criterion
the game measures, and the common mistakes with the feedback line the game shows. "12" is the
zenith; kite elevation is in degrees above the water. Winds are the lesson's default and the
player can raise them.

### A. Riding (10 to 14 kn)

| # | Lesson | Needs | Drill | Pass (**estimate**) | Mistake → feedback |
| --- | --- | --- | --- | --- | --- |
| A1 | Kite power dive | — | Standing; ghost-kite arc from 12 to 45° and back, both sides | 6 dives; never below 30°; back above 70° within 2 s | Too deep or too slow back → "Turn it back up before 45°" |
| A2 | Water start | A1 | Floating, board downwind, kite at 12; one prompt to dive | Planing within 4 s, then 50 m on each tack | Board square to the pull → "Point the nose at the kite". Nosedive → "More weight on the back foot" |
| A3 | Speed control | A2 | Hold a speed band shown on the HUD with the bar only | Within ±15% of the target for 20 s, kite between 35 and 55° | Kite parked high → "Kite too high: fly it at 45°" |
| A4 | Upwind | A3 | Gate to a buoy upwind; wind arrows on the water | 50 m net upwind gain on each tack at planing speed | Carved up straight after the start → "Plane first, then carve upwind". Speed collapsed → "Ease the edge" |
| A5 | Transition | A4 | Three short prompts: slow down, switch, steer the kite across | 3 turns keeping 70% of the speed, no fall | Sank after → "Steer the kite faster through the turn" |
| A6 | Toeside | A5 (needs toeside riding, tricks T3.7) | Free ride with a toeside meter | 100 m toeside | Front hand steering → "Back hand flies the kite" |
| A7 | Carving transition | A5 | Timing bar of kite turn against board carve | Turn without dropping below planing | Kite early → "The kite left the power too soon". Kite late → "You sank waiting for pull" |

### B. Jumps (12 to 16 kn)

| # | Lesson | Needs | Drill | Pass (**estimate**) | Mistake → feedback |
| --- | --- | --- | --- | --- | --- |
| B1 | Pop | A4 | Sub-drills: carve only (spray-arc meter), stomp only (load and release) | 0.5 m or more with the kite moving under 10° during take-off | Kite rose → "You lifted the kite: pop with the board". Skipped → "Too fast: edge earlier" |
| B2 | Small jump | B1 | Slow send; a "sheet in at 12" timing ring | 1 to 2 m, Clean, five in a row | Sheeted in while the kite climbed → "Bar out while it climbs, in at 12". Edge lost → "Hold the edge until take-off" |
| B3 | Landing dive | B2 | Slow motion at 1.5 m on the way down | 3 to 5 m, Clean, dive started in the last second | Early → "Dived early: the kite was too low to catch you". Late → "Dived late: no pull, you sank". Front stall → "The kite overflew" |
| B4 | Jump and grab | B3, grabs (T2.1) | Grab buttons live only in the air | Grab held 0.5 s, kite within 15° of 12, Clean | Kite drifted → "Hold the kite at 12 while you grab" |
| B5 | Jump transition | B3, A5 | — | Lands Clean heading the other way | Too much crosswind speed → "Pop harder to kill your speed". Kite past 12 → "No room to dive" |
| B6 | Downloop transition | B5 | Very light wind; ride downwind first | Loop completed, new direction, no fall | Let go mid-loop → "Keep steering until the kite climbs" |

### C. Rotations (12 to 16 kn), after tricks T1

| # | Lesson | Needs | Drill | Pass (**estimate**) | Mistake → feedback |
| --- | --- | --- | --- | --- | --- |
| C1 | Back roll | B2 | Carve about 90° upwind before leaving the water | 330 to 390°, Clean | Steered mid-rotation → "Bar straight: kite stays at 12". Under-rotated → "Carve further upwind at take-off" |
| C2 | Front roll | C1 | — | 330 to 390°, Clean | Under-rotated → "Lift the front foot, drag the back" |
| C3 | Roll transitions, double back roll | C1, C2, B5 | — | Each landed Clean once; the double turns 690 to 750° | — |

### D. Big air (16 to 22 kn)

| # | Lesson | Needs | Drill | Pass (**estimate**) | Mistake → feedback |
| --- | --- | --- | --- | --- | --- |
| D1 | Hard-edge take-off | B3 | Kite low, fast send | 6 m or more, edge held to the last 0.3 s | "Edge released early: height lost" |
| D2 | Two-movement landing | D1 | Two prompts: back hand at 4 m, front hand at 1 to 2 m | 6 m or more, Clean, sink 3 m/s or less | "The kite crept upwind: back-hand steer before you dive" |
| D3 | Heli loop landing | D2 | Delayed heli in about 10 kn, then a full one | Clean, kite in the landing sweet spot | Late → "Loop late: kite still low". Away from 12 → "Loop towards 12" |
| D4 | 10 m | D2 or D3 | 18 to 22 kn | 10 m or more, Clean | — |
| D5 | Sent jump off a kicker | D4, kickers | Ramp lip timing | Sheet in within 0.15 s of the lip; 12 m or more | "Sent too early: the kite passed 12 before the lip" |

### E. Kite loops (10 to 20 kn)

| # | Lesson | Needs | Drill | Pass (**estimate**) | Mistake → feedback |
| --- | --- | --- | --- | --- | --- |
| E1 | Micro loops | B6, D3 | Downwind loops, then from a jump of 1 m or less | Loop held until the kite points up; 1.5 s or less | "Hold the steer until the kite points up" |
| E2 | Loop and catch | E1 | 1 to 2 m jump | Kite back above 70° before touchdown, Clean | "Bar out after the loop: let it fly forward" |
| E3 | Powered loop into heli | E2, D3 | — | Loop before the apex, heli landing, Clean, three in a row | Spinning at take-off → "Don't commit the loop while rotating" |
| E4 | Megaloop | E3, D4 | — | 10 m or more, the classifier says megaloop, Clean | — |
| E5 | Back roll kite loop | E3, C1 | — | Roll plus loop, Clean | — |

### F. Unhooked (14 to 18 kn), after tricks T3

| # | Lesson | Needs | Drill | Pass (**estimate**) | Mistake → feedback |
| --- | --- | --- | --- | --- | --- |
| F1 | Unhook and raley | C3, E1 | Unhook while bearing away; 100 m unhooked; then raley | Body 60° or more from upright; bar slack at touchdown | Kite climbed → "Hands lower on the bar". No bear-away → "Bear away before you unhook" |
| F2 | Surface pass, pop pass, 313 | F1 | Pass input drill on land first | Pass done while the lines are slack; 313 named and Clean | "Lines too tight: head downwind and pull towards the kite" |

The trick lessons that come after these (grab variations, board-offs, KGB and so on) use the same
lesson format. Each one is generated from the trick book's names, with the trick's prerequisite
from `docs/tricks.md` as its gate.

## 3. Design

### 3.1 Shape of the feature

- **A "School" entry in the main menu, and in the pause menu during a ride.** It opens the lesson
  menu, where any unlocked lesson can be started or rerun at any time:
  - **Chapter map:** six chapters, each lesson a tile showing its stars (0 to 3), locked or
    unlocked, and a "new" badge. Lessons unlock by prerequisite, not strictly in order, so the
    player can branch: rotations and big air are both open after the landing dive.
  - **Lesson detail panel:**
    - what the lesson teaches, its prerequisite and its pass criterion;
    - the player's best result: best stars, best value such as height or landing grade, attempts,
      passes, and when it was last played;
    - **Start**, **Watch demo**, and a **wind and assists** selector for reruns with more wind or
      fewer assists.
  - **Continue:** a button at the top that jumps to the recommended next lesson. That is the
    lowest-numbered unlocked lesson without a star, or one with fewer stars than the player's
    usual.
  - **Overall progress:** the chapter completion percentage, total stars, and the trick book count.
  - **Reset progress**, with a confirm. It never resets the trick book.
  - **Navigation:** keyboard and gamepad through `FKiteMenuNavigator`, like the other menus.
  - **Within a lesson:** the pause menu offers Retry, Lesson menu and Free ride. The result card
    after a pass offers Next lesson, Retry for more stars, and Lesson menu.
- **Each lesson is a short scene:** a set-up, one to three drills, and a pass test.
  - The set-up fixes the map, the wind, the gear, the start state and the assists.
  - Every lesson takes one to three minutes. Free riding is always one button away.
- **The first-run tutorial** becomes lessons A1 to A3 plus a free "first jump" moment from B2.
  This replaces today's four-step HUD onboarding.
- **Stars:**
  - 1 star: pass.
  - 2 stars: pass with fewer assists or a higher bar (Clean becomes Stomped).
  - 3 stars: pass with every assist off.
  - The trick book records the first landing of each trick, and a lesson pass unlocks the trick in
    the book (backlog G4).

### 3.2 Lesson data

A lesson is data, not code, so new lessons are table rows. A C++ table is simplest (like the
trick naming table); a `UDataAsset` per lesson is the alternative.

```cpp
struct FLessonSetup {
    FName Map;                 // L_FlatWater for most lessons
    float WindKnots;
    EKiteModel Kite;
    float KiteSizeM2;
    EBoardSize Board;
    ELessonStart Start;        // Standing, Floating, Riding(speed, tack), Airborne(height)
    FLessonAssists Assists;    // auto-park, auto-edge, landing assist, auto-redirect, loop catch, slow-mo
};

struct FLessonStep {
    FText Prompt;              // a few words plus an input glyph
    ELessonCue Cue;            // GhostKite, WindowArc, TimingRing, SpeedBand, Gate, None
    FLessonObjective Objective;
};

struct FLessonObjective {
    ELessonMetric Metric;      // JumpHeight, LandingGrade, KiteElevationBand, SpeedBand, UpwindGain,
                               // TrickName, LoopKind, Rotation, GrabHold, Repetitions ...
    float Min, Max;
    int32 Count;
    float WindowSeconds;
};

struct FLessonDef {
    FName Id;                  // "B3"
    FText Title;
    FName Chapter;
    TArray<FName> Requires;
    FLessonSetup Setup;
    TArray<FLessonStep> Steps;
    FLessonObjective Pass;
    TArray<FLessonFault> Faults;   // ordered diagnosis rules → feedback line (3.4)
    FStarRules Stars;
};
```

### 3.3 Runtime

- **`ULessonSubsystem`** (a game instance subsystem):
  - owns the progress, saved in `UKiteSurfSaveGame` as stars per lesson;
  - knows which lessons are unlocked;
  - starts a lesson: it opens the map with the set-up, the way the gear screen applies a ride.
- **`ALessonDirector`** (spawned in the ride level while a lesson runs):
  - applies the set-up through the pawn's existing APIs: `AKiteSurfGameMode::InitializeRide`,
    `SetKiteSize`, the wind, the assist toggles;
  - steps the lesson state machine each frame;
  - evaluates objectives by polling, as the trick tracker does;
  - drives the HUD cues;
  - on failure, picks a fault line;
  - offers to drop back a step after three failures (the channel's own advice);
  - all of it runs without a renderer, so lessons are testable under `-nullrhi`.
- **Data sources already on `main`:**
  - `UTrickTrackerComponent` and `FJumpRecord`: height, airtime, landing grade and cause, loops,
    trick name;
  - the board and kite getters: speed, edge, kite clock and elevation, tension, take-off and
    landing facts;
  - the trick book.

  The lesson system adds no new physics.
- **Demonstrations (later, S11):** "show me" plays the lesson's demonstration on the player's own rider.
  - It is a scripted input track run through `ApplyScriptedInput` (what `kitesurf.Input` and the
    menu-video recorder already use), with a ghost kite drawn along.
  - Then control returns at the same spot.
  - A recorded ghost rider (backlog G2) can replace this later.

### 3.4 Feedback

- **Wind-window arc.** It grows from the existing HUD arc and marks target zones: the 45° cruise
  band, the 12 o'clock sheet-in band, and the landing sweet spot just in front of 12. It fades out
  as stars are earned.
- **Ghost kite.** A translucent kite at the target position. It follows the expert run by phase
  (send, 12, hang, dive), not by wall clock.
- **Timing grades.** The moment of the action shows Early, Good, Perfect or Late, OlliOlli style:
  - sheet in against the kite reaching 80 to 90°;
  - the stomp against peak edge load;
  - the back-hand steer at about 4 m and the front-hand dive at 1 to 2 m;
  - the loop start against the apex.
- **Slow motion.** Only at the lesson's one decision point (the kite crossing 12, the 4 m landing
  cue), with one prompt. It switches off after three Clean attempts.
- **Diagnosis rules.** Each common mistake is a test on the jump record or the last seconds of
  telemetry:
  - dived early: kite under 45° at touchdown;
  - front stall: kite upwind of the rider at touchdown;
  - edge lost: edge drop before take-off;
  - lifted the kite on a pop: kite elevation change at take-off.

  The landing evaluator's causes cover the rest. Exactly one line is shown, the highest-priority
  rule that matched; this is backlog F6.
- **Replay** (later): an automatic replay of the last attempt with event markers on a strip
  (edge released, sheet-in, kite at 12, dive, touchdown) and the one fault callout. It needs the
  recorder in backlog G2.

### 3.5 Assist ladder

Assists start on and peel off lesson by lesson; stars reward turning them off:

- auto-park and auto-edge in chapter A;
- the landing assist and auto-redirect from chapter B (physics phase 3 adds the redirect);
- loop catch in chapter E.

The same toggles live in Settings for free ride (backlog D6).

## 4. What it needs from the rest of the game

| Need | From | State |
| --- | --- | --- |
| Jump record, landing grade and cause, loop kinds, trick names | Tricks #60 to #69, #73 | On `main` |
| Rotation (chapter C) | Tricks T1 (rotation live, input) | In progress |
| Grabs (B4) | Tricks T2.1 | Planned |
| Toeside riding (A6) | Tricks T3.7 | Planned |
| Unhooked, raley, passes (F) | Tricks T3 | Planned |
| Auto-redirect assist (D2 to D3) | Physics phase 3 item 2 | In progress |
| Kickers (D5) | Spot work, backlog C9 | Not started |
| Floating start and standing start (A1, A2) | Board floating state (phase 2/3); standing on a beach does not exist | Partly. A1 can start floating or riding slowly |
| Scripted input for demonstrations | `ApplyScriptedInput`, `kitesurf.Input` | On `main` |

## 5. Implementation plan

Each task ships with `KiteSurf.School.*` automation tests and follows the merge-to-main rule.

| ID | Task | Acceptance | Size | Depends on |
| --- | --- | --- | --- | --- |
| S1 | **Done.** Lesson data types, objective and fault evaluators as pure functions over `FJumpRecord` and a telemetry sample buffer; chapters A and B as data (`School/`, `KiteSurf.School.*` tests) | Each metric and each fault rule tested on synthetic records | M | — |
| S2 | `ULessonSubsystem`, per-lesson progress in the save game (best stars, best value, attempts, passes, last played), unlock graph, recommended next lesson, reset | Progress persists across a save round trip; prerequisites unlock; the recommended lesson follows the rule; reset clears lessons but not the trick book; an old save loads with no progress | S | S1 |
| S3 | `ALessonDirector`: set-up, state machine, pass and fail, drop-back offer | A scripted ride passes lesson B2 under `-nullrhi`; a bad send fails with "Bar out while it climbs" | M | S1, S2 |
| S4 | HUD lesson layer: prompt with glyph, objective progress, window-arc target zones, ghost kite, timing grades, result card with stars | `KiteSurf.HUD.Lesson*` text and state tests; seen in a `-game` run | M | S3 |
| S5 | School menu from the main and pause menus: chapter map, tiles with stars and lock state, detail panel with best result and attempts, Start, Watch demo, wind and assists for reruns, Continue, overall progress, reset with confirm, Retry, Next and Lesson menu on the result card | Menu navigation tests like the gear screen's; a locked lesson cannot start; rerunning a passed lesson keeps the best stars; seen in a `-game` run | M | S2 |
| S6 | Chapters A and B as data (A6 waits for toeside riding, B4 for grabs), text prompts only | A scripted ride passes each lesson's test | M | S3, S4 |
| S7 | Replace today's four-step onboarding with A1 to A3 and a guided first jump; keep the skip option | First run starts lesson A1; "skip" goes to free ride | S | S6 |
| S8 | Slow motion at decision points | Time dilation on and off around the cue; the simulation stays deterministic in fixed steps | S | S3 |
| S9 | Chapters C to F as data, as their trick features land | Per lesson, as S6 | M each | Tricks T1, T2.1, T3; phase 3 redirect; kickers |
| S10 | Replay of the last attempt with event markers | Needs the backlog G2 recorder | L | G2 |
| S11 | Bot demonstration: before a lesson, a bot rider performs the skill from a scripted input track, filmed with the cinematic cameras (`AKiteSurfCinematicCamera`, as the menu video does), then hands over to the player at the same spot | Each lesson's demonstration track passes its own pass test in a scripted run; seen in a `-game` run | L | S6 |
| S12 | Voice-over for demonstrations and prompts, with subtitles; the text prompts stay | Each line has an audio asset and a subtitle; volume follows the existing volume settings | M | S11 |
| S13 | Input animation: a small on-screen gamepad, keyboard or mouse that shows the stick, trigger or mouse movement for the current step, matching the player's active control scheme | Animation matches the bindings in `make_input_assets.py` for each scheme | M | S4 |

Order: S1 → S2 → S3 → S4 and S5 in parallel → S6 → S7. The first round is S1 to S7, text only.
Demonstrations, voice and input animation (S11 to S13) come after it. S8 can go any time after S3, and S9
follows the trick work. S1 to S7 touch no physics, so they can run alongside physics phase 3 and
the trick wiring.

## 6. Open questions

- [x] **Where lessons live.** `L_FlatWater` for every lesson (decided 2026-10-03). A school spot
  can come later, with kickers.
- [x] **Narration.** The first round is text only: short prompts with input glyphs (decided
  2026-10-03). Later, each lesson gets a demonstration before the player tries: a bot rider
  performing the trick with cinematic cameras and a voice-over, plus an animation of how to move
  the controller or mouse (tasks S11 to S13).
- [x] **Unhooked gate.** Keep Kitesurf College's order: chapter F needs chapters C and E (decided
  2026-10-03).
- [x] **Stars against assists.** Three stars means every assist is off (decided 2026-10-03).

## Sources

- **Kitesurf College:** the playlists linked in section 1; cues paraphrased from the captions of
  the pop, jumping, jump transition, back roll, front roll, jumping higher, heli loop, kite loop,
  unhooking, raley and surface pass lessons.
- **IKO:** the levels as published by schools, e.g. [Accrokite](https://accrokite-kohphangan.com/iko-levels/)
  and [Paje](https://pajekitesurf.com/practice/lessons_plan/).
- **VDWS:** [basic licence](https://www.vdws.de/en/basic-licence/kitesurfing).
- **Coaching:** [MACkite raley guide](https://www.mackiteboarding.com/news/the-kiteboarding-raley-ultimate-beginners-guide-unhooked/),
  [Peter's Kiteboarding progression](https://kitesurfing-handbook.peterskiteboarding.com/progression).
- **Games:**
  - [Trials Rising review](https://www.pcgamesn.com/trials-rising/review)
  - [Skate 3](https://en.wikipedia.org/wiki/Skate_3)
  - [Riders Republic tips](https://www.digitaltrends.com/gaming/riders-republic-tips-and-tricks-to-get-started/)
  - [OlliOlli World](https://www.nme.com/features/gaming-features/olliolli-world-is-a-kinder-gentler-olliolli-and-its-all-the-better-for-it-2987379)
  - [skate. preview](https://gamerant.com/skate-gameplay-preview/)
  - [THPS 1+2 review](https://switchplayer.net/2021/07/03/tony-hawks-pro-skater-1-2-review/)
  - [Winds Up Kitesurfing](https://store.steampowered.com/app/2799240/Winds_Up_Kitesurfing/)
- **Onboarding:** [Celia Hodent](https://celiahodent.com/gamers-brain-ux-onboarding/).

Not verified:
- three Kitesurf College lessons are members-only and were not read;
- the game tutorial details come from reviews and wikis, not play;
- every pass threshold is an estimate.
