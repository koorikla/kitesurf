# Storm jumps: why height levels off above 60 kn

The wind now goes to 90 kn and the height ceiling is at the cloud base (PR 46), but jump height
stops growing with the wind. This note records what was measured at `main` commit `10a5d0a` so
that the phase 2 work on the kite in the air (`plan-2.md` item 2) can be checked against it.
Every number is from the `-nullrhi` automation fixtures (`RunJump` in `RideLoopTests.cpp`:
recommended loop kite, kite sent, edge held, release swept in 0.1 s steps); none was seen in a
`-game` run.

## What happens

Best-timed jump at each wind:

| Wind | Kite | Best release | Height | Distance | Airtime |
| --- | --- | --- | --- | --- | --- |
| 15 kn | 12 m | 1.6 s | 6.1 m | 21 m | 2.7 s |
| 30 kn | 6 m | 0.8 s | 14.9 m | 53 m | 4.1 s |
| 40 kn | 5 m | 0.5 s | 19.4 m | 88 m | 4.7 s |
| 60 kn | 3 m | 0.3 s | 27.6 m | 171 m | 7.6 s |
| 75 kn | 2 m | 0.2 s | 26.9 m | 194 m | 7.4 s |
| 90 kn | 2 m | 0.3 s | 30.2 m | 256 m | 7.9 s |

Distance keeps growing with the wind; height does not. A kite far too big for a storm (3 to 9 m
in 90 kn) does not go higher: it jumps 1 to 5 m.

## Why

Traces of the 30, 60 and 90 kn jumps (4 Hz samples of the fixture's `FJumpSample`):

1. **Take-off speed is the same in any wind.** Vertical speed at take-off is 12.9, 11.9 and
   13.9 m/s at 30, 60 and 90 kn. The pop is the legs plus `EdgeReleaseSeconds` (0.22 s) times
   the upward line force at the release, and at the best moment the kite is only 37 to 45 deg
   up, so that force is about the same whatever the wind.
2. **The kite is low and the lines go slack in bursts for the first two seconds.** At 60 and
   90 kn the kite sits at 35 to 46 deg elevation until about 2.5 s after take-off, and every
   other sample in that time has zero tension (the rider leaves the water faster than the kite
   climbs, and the taut-only line constraint lets go and catches). This is when the wind the
   kite feels is still strong (70 down to 30 m/s airspeed at 90 kn), so it is where the height
   would come from.
3. **After about three seconds the rider is moving with the storm.** Air drag on the rider
   (`RiderDragAreaM2` 0.7) carries them downwind; the kite's airspeed settles at 17 to 21 m/s
   and it carries 0.6 of the rider's weight for the rest of the flight. That gives the long
   float and the distance, and no more height. This part is physical.

## What does not fix it

Swept at 30, 60 and 90 kn, best release each:

| Change | 30 kn | 60 kn | 90 kn |
| --- | --- | --- | --- |
| baseline | 14.9 m | 27.6 m | 30.2 m |
| `MaxLineTensionN` 6000 to 12000 | 14.9 m | 27.6 m | 30.9 m |
| `EdgedLiftoffWeightBonus` 3 to 6 | 15.9 m | 29.6 m | 31.4 m |
| both | 16.1 m | 27.6 m | 30.9 m |
| both, and `RiderDragAreaM2` 0.7 to 0.35 | 17.5 m | 32.5 m | 38.9 m |

The tension cap and the edge hold are not what limits a storm jump. Halving the rider's drag
helps most, but 0.7 m^2 is the researched value and should stay.

## What should fix it

Points 1 and 2 are the two things `plan-2.md` item 2 changes: the kite held overhead in the air
(A2) and the climb coming from the line force after the release instead of a fixed 0.22 s
impulse (A3). With the kite overhead and the lines taut through the first two seconds of a
90 kn jump, the lift available is about `0.5 * 1.225 * 1.0 * 2 * 0.72 * 46^2` = 1.9 kN, 2.3
body weights, falling as the rider picks up the wind's speed.

Suggested checks to add when that lands (the targets are a design choice for the user, not a
physical law):

- Best-timed height grows with the wind from 30 to 90 kn, with no plateau: each of 40, 60 and
  90 kn at least 20% above the one before.
- Lines taut for at least 80% of the first two seconds of a 60 kn and a 90 kn jump.
- The rider still comes down, the kite stays flying, and 30 kn stays at 10 to 20 m.

## After phase 2

Measured again on `physics/phase2` merged with `main` (the same fixture: recommended loop kite,
kite sent, now with the jump button held and let go, and a crouched landing; release swept in
0.05 s steps). Phase 2 put the kite overhead in the air (A2), took the 0.22 s pseudo-impulse out
(A3), made the loaded rider hang on until the lines pull `1 + LoadHoldBonus` body weights, and
made landings a real g with a crash over `CrashLandingG` (8 g).

| Wind | Kite | Best release | Height | Distance | Airtime | Landing |
| --- | --- | --- | --- | --- | --- | --- |
| 30 kn | 6 m | 0.65 s | 10.3 m | 66 m | 5.0 s | 6.3 g, hot |
| 40 kn | 5 m | 0.45 s | 15.9 m | 102 m | 5.9 s | 10.4 g, a crash (13.0 m at 0.35 s lands at 7.9 g) |
| 60 kn | 3 m | 0.25 s | 23.9 m | 161 m | 6.8 s | 16.3 g, a crash (10.0 m at 0.10 s lands at 6.1 g) |
| 75 kn | 2 m | 0.20 s | 23.5 m | 175 m | 6.8 s | 16.5 g, a crash (9.7 m at 0.05 s lands at 6.7 g) |
| 90 kn | 2 m | 0.20 s | 22.4 m | 225 m | 7.4 s | 14.4 g, a crash; nothing over 2 m lands |

Height still levels off above 60 kn, lower than before (27.6 and 30.2 m at 60 and 90 kn), and a
release later than these is now pulled off the water by the kite first (from 0.25 s in 90 kn).
The storm jumps come down sinking 12 to 13.5 m/s and crash even crouched: this is phase 2's
descent gap (`CHANGELOG.md`, known gaps), the kite overhead not holding the rider up on the way
down, at its largest. `KiteSurf.Wind.StormIsRideable` lets go at 0.2 s.
