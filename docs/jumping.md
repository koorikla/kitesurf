# Jumping Mechanics & Hang-Time

This document describes the kite-powered jump mechanics, airborne trajectory, landing evaluation, and crash recovery implemented in `UBoardMovementComponent` and `AKiteRiderPawn`.

## Jump Mechanics Lifecycle

### State Machine
The board lifecycle transitions through four distinct states in `EBoardState`:
1. **Displacement**: Speeds below the planing threshold (< 400 cm/s).
2. **Planing**: Speeds $\ge 400 \text{ cm/s}$ skimming over the surface.
3. **Airborne**: Active pop off the water, or the lines lifting the rider off; the water's forces are off while above water surface + 10 cm, and the air drags on the rider instead.
4. **Landing**: 0.25 s after a clean touchdown, while the touchdown absorber takes the sink out; or the crash recovery.

### Pop
A rider can always pop while they are up on the board on the water: no edge and no minimum speed are needed. `Jump()` is refused (`NotPlaning`, shown as "Get up on the board first") only in the air, during a crash, or while floating. A part-sunk board gives proportionally less push.

### Load
Holding the jump button (`AKiteRiderPawn::SetLoadHeld`, `UBoardMovementComponent::SetLoadHeld`) puts the rider into a crouch with their weight over the back of the board: the jointed rider's pelvis drops and the knees bend. `GetLoadAmount()` builds to 1 over 0.4 s (`LoadRatePerSec`) and lets go at `LoadReleaseRatePerSec`. While loaded:
- the board heels `LoadExtraHeelDeg` (25 deg) past the balance of the pull across it (up to `MaxHeelDeg`, 65), so the water's normal force pushes it to windward of its heading, the apparent wind goes aft, the kite sits deeper and the lines pull harder. How much depends on where the kite is: an edge resists a pull across the board, not one along it. Riding the 9 m in 20 kn with the kite held at its clock position, the heel goes from 32 to 59 deg, the board moves 61 cm/s to windward and tension rises from 541 to 672 N (`KiteSurf.Jump.LoadAndRelease`); on the 12 m in 15 kn 1.5 s of load raises it from 448 to 605 N (`KiteSurf.Physics.LoadingBuildsTension`);
- the rider hangs on against the lines: up to `LoadHoldBonus` (1.5) more body weights of upward pull, their legs holding the board on the water (see below);
- the pop that follows is up to `LoadPopBonus` (60%) stronger: 4.0 m/s against 2.5 m/s from a tap.

Letting go of the button pops (`ReleaseLoadAndPop`). If the kite has already pulled the rider off the water, letting go does nothing more.

Held in the air, the button is the crouch for the landing: the load builds there too, changes nothing in the air, and lengthens the touchdown's absorb distance by `CrouchAbsorbBonus` (a full crouch doubles it; see Landing below). Let go of it on the water and it pops again, unless the board is still in its 0.25 s landing state.

Vertical jump impulse is imparted as:
$$J_z = \text{PopImpulseKgCmPerS} \cdot (1 + \text{TailWeightPopBonus} \cdot \text{tail weight}) \cdot (1 + \text{LoadPopBonus} \cdot \text{load}) + \text{EdgeReleaseSeconds} \cdot \max(0, F_{\text{kite}, z})$$
$$\Delta v_z = \frac{J_z}{m}$$

The legs give about 2.5 m/s (3.7 m/s with the weight on the tail, 5.9 m/s from a full load with the weight on the tail), a hop of under a metre. The second term is off by default (`EdgeReleaseSeconds` 0, `docs/physics/plan-2.md` item 2, A3): once the rider is off the water the lines' pull acts on them as a force, and that is all the kite adds. At phase 1 it was 0.22 s of the upward pull counted again as an impulse, which gave the timed jump at 30 kn 5.7 of its 10 m/s of take-off speed; set it back for that feel.

### Loading the edge
- The kite lifts the rider off the water by itself when its upward pull passes their weight.
- A loaded rider (the jump button held) hangs on to more: the board leaves the water when the upward pull passes $m g (1 + \text{LoadHoldBonus} \cdot \text{load})$, 2.5 times their weight at full load (`docs/physics/plan-2.md` item 3; research take-off at 2.5 to 4 body weights of tension). That is what lets the pull build while the kite is steered up. Until then the legs hold the board on the water: 20 cm above its ride height they hold it down with up to `LoadHoldBonus` times the load times their weight and stop it rising there (since plan-2 item 4; it was the 20 cm water-contact clamp). An edge alone (the turn input or the weight on the tail) no longer holds them down (`KiteSurf.Jump.EdgeHoldsRiderDown`).
- Once the lines lift more than the rider weighs the board carries no weight, the water's normal force goes and only the fins and the rail hold the edge (weight on the tail buries the rail, `TailWeightRailScale`): the longer the hold, the more the board slides towards the kite.
- Releasing the edge with a pop while the lines are loaded is the big jump. Releasing early gives less; holding on until the kite pulls the rider off the edge loses the pop and the timing, and is far lower.
- The kite answers the send after its steering dead time (0.24 s at the start's 70% sheet), so the release is timed from when the kite starts to move. With the recommended kite (loop model), sending the kite hard with the jump button held and the weight on the tail, letting go at the best moment and crouching again for the landing: about 4.4 m in 15 kn (1.74 s after the kite answers; landed at 4.2 g), 10.7 m in 30 kn (0.66 s; from 0.70 s the lines pull the rider off first, at 2.5 body weights; landed at 6.7 g, hot) and 15.9 m in 40 kn (0.46 s), which lands at 10 g even crouched, a crash: the highest that lands there is 13.1 m (0.36 s, 7.9 g). The test's 0.66 s release at 30 kn goes 10.7 m with 5.0 s in the air; 0.64 s gives 9.9 m. A pop with the kite parked is 1.0 m, sending the kite without an edge 3.0 m, letting go at 0.3 s 2.4 m, and holding on to 3 s gets the rider pulled off at 4.9 m (`KiteSurf.Jump.TimedReleaseBeatsPop`).

### Airborne Dynamics & Apex Envelope
- The line force continues to act on the rider. The pawn tells the kite when the rider is in the air (`UKiteComponent::SetRiderAirborne`), and with the bar centred the kite's assist then flies it to 12 over them and holds it there (`AirborneZenithGain`, `AirborneZenithMaxHeadingDeg`; 0 gain turns that off), so it carries most of their weight on the way down. Bar over still flies it round the window, and a loop is still the rider's.
- The air drags on the rider and board, $0.5 \rho C_D A |v_a| v_a$ with `RiderDragAreaM2` (0.7 m^2) and $v_a$ the wind at chest height (`UKiteComponent::RiderWindHeightCm`) minus their velocity, sampled at the board's simulation time.
- A little slack in the lines does not drop the kite: the canopy keeps flying and takes the slack back up. Only with more than `SlackCollapseCm` of slack is it a loose sheet that falls.
- While airborne (> 10 cm above water surface), water buoyancy and water drag forces are disabled.
- The trajectory is clamped at `MaxJumpHeight` (4000 cm = 40 m), with upward velocity zeroed if the ceiling is reached.

### Hang time
Effective gravity $8h/t^2$ tells how much of the rider the kite carries: real jumps give 1.5 to 3.5 m/s^2 (the kite carrying 65 to 85% of the rider). The timed jump at 30 kn gives 3.46 m/s^2, 10.7 m in 4.98 s (`KiteSurf.Physics.HangTime`, which logs the jump at 20 Hz): the lines hold up 0.70 of the rider's weight over the flight, and on the way down the kite is about 71 deg above them, flying unstalled (`KiteSurf.Physics.KiteOverheadInTheAir`). It is above 60 deg 2.1 s after take-off. Over the last 1.75 s it sinks from 84 to 55 deg above the rider and their sink grows to 8.2 m/s at touchdown, where the research has 3 to 6 m/s under a kite held overhead: a known gap (`docs/physics/CHANGELOG.md`). At phase 1 the assist steered by the wind the rider feels, which in the air is dominated by their own climb and fall, and the same jump gave 7.8 m/s^2. A loop flown from the apex is a known gap: it peaks at about 0.9 body weights with the kite still 60 deg up, against the research's 3 to 5 with the kite low (`KiteSurf.Physics.AirborneLoopYanks`).

### Landing Evaluation & Crash Recovery
Upon coming down to the water (sinking into it relative to its surface, which on a swell may be rising, and within 10 cm of it; `docs/physics/plan-2.md` item 4, `docs/physics/research.md` 3.4):
- **The sink** $v$ (m/s, relative to the surface) is taken out by the touchdown absorber at a constant $v^2 / (2 s)$ over the absorb distance $s$: `LandingAbsorbDistanceCm` (30 cm, the legs and the board's immersion; research 0.2 to 0.4 m) times $1 + \text{CrouchAbsorbBonus} \cdot \text{crouch}$ (1: a full crouch doubles it). The board goes $s$ on into the water and comes back up on the buoyancy and the planing lift; nothing is snapped or zeroed.
- **Landing g**: $1 + v^2 / (2 g s)$, what `OnBoardLanding` reports, `GetLastLandingG()` holds (with `GetLastLandingSinkMS()` and `GetLastLandingAbsorbCm()`) and the HUD's landing card shows. Standing, 2 m/s is 1.7 g, 4 m/s 3.7 g and 6 m/s 7.1 g (the ratio of the squares); crouched 1.3 and 4.0 g; at 6 m/s the water's push on the board peaks at the landing's g and the board goes 31 cm on into the water (`KiteSurf.Physics.LandingGFromSink`).
- **Hot** (`WasLastLandingHot()`): sinking faster than `HotLandingSinkMS` (6 m/s), or with the kite under `HotLandingKiteElevationDeg` (45 deg) above the rider. A flag, not a crash.
- **Landing Angle**: between the horizontal velocity and the board's axis, either way round ($0..90^\circ$).
- **Clean Landing** (Angle $\le$ `MaxLandingAngle` (30 deg) and the landing g at most `CrashLandingG` (8)):
  - Rider retains 80% horizontal speed (`CleanLandingSpeedRetention = 0.80`).
  - The board is in the Landing state for 0.25 s while the absorber works, then planing or displacement.
- **The timed jump at 30 kn** (crouched from the apex) touches down sinking 8.2 m/s with the kite 55 deg up: 6.7 g, hot, ridden away (`KiteSurf.Physics.GoodLandingIsThreeToSixG`). The target is 3 to 6 g (measured landings 4.2 to 5.5 g); the descent, not the landing model, is short (known gaps). Standing it would be 12.3 g, a crash: land big jumps crouched. Brought down to the side from the apex, the kite is 37 deg up at touchdown and the landing is hot by the kite too.
- **Crash Landing** (Angle $> 30^\circ$, or more than `CrashLandingG`):
  - Speed decelerates linearly to 0 over `CrashDecelDuration` (0.5 s).
  - Rider stays at crash location for `CrashRespawnDelay` (1.0 s), 1.5 s from the crash in all.
  - Rider respawns upright (pitch=0, roll=0, at water level) at 8 kn on the tack they were on (`ResetToTack`), with the kite parked at 45 degrees on that side.

## Default Tunable Properties

Exposed in `UBoardMovementComponent` under `UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning|Jump")`:

| Property | Default Value | Description |
| :--- | :--- | :--- |
| `PopImpulseKgCmPerS` | `21000` | Pop impulse from the legs in kg*cm/s (about 2.5 m/s on 85 kg). |
| `TailWeightPopBonus` | `0.5` | Extra pop with the weight fully on the tail, as a fraction of the pop. |
| `EdgeReleaseSeconds` | `0` | Seconds of the kite's upward line force counted again as an impulse when the edge lets go (s). 0 is physics only; phase 1 used 0.22. |
| `LoadHoldBonus` | `1.5` | Upward pull, in rider weights above their own, that a fully loaded rider hangs on to before the lines lift them off. |
| `RiderDragAreaM2` | `0.7` | Drag area of the rider and board in the air (m^2). |
| `AirSpinRate` | `200` | Board spin in the air at full carve (deg/s). |
| `AirWeightShiftPitchDeg` | `30` | Board pitch at full weight shift in the air (deg). |
| `MaxJumpHeight` | `4000` | Maximum jump apex height clamp in cm (40 m). |
| `MaxLandingAngle` | `30` | Maximum deviation angle in degrees between velocity and board heading for clean landing. |
| `LandingAbsorbDistanceCm` / `CrouchAbsorbBonus` | `30` / `1.0` | The distance a touchdown's sink is taken out over (cm), and how much longer a full crouch makes it (fraction). |
| `CrashLandingG` | `8` | A landing harder than this (g) is a crash. |
| `HotLandingSinkMS` / `HotLandingKiteElevationDeg` | `6` / `45` | A landing sinking faster than this (m/s), or with the kite lower than this (deg), is hot. |
| `LoadRatePerSec` / `LoadReleaseRatePerSec` | `2.5` / `6.0` | How fast the crouch builds while the jump button is held, and lets go (1/s). |
| `LoadPopBonus` | `0.6` | Extra pop from a full load, as a fraction. |
| `CleanLandingSpeedRetention` | `0.8` | Fraction of horizontal velocity retained on clean landing (80%). |
| `CrashDecelDuration` | `0.5` | Duration in seconds to decelerate to zero upon crash landing. |
| `CrashRespawnDelay` | `1.0` | Time in seconds after the deceleration before the rider respawns upright (1.5 s in all). |

## HUD Telemetry
`AKiteSurfHUD` renders:
- Current board state: `Displacement`, `Planing`, `Airborne (<height>m)`, or `Landing (Clean/Crash!)`.
- Jump stats: `Best <height>m` and `Apex <height>m`.
- The landing card, for `LandingCardSeconds` (3 s) after each landing from a jump: `LANDED 4.2 g`, with `HOT` after it for a hot landing (the card orange), or `CRASH 9.2 g` (`FormatLandingCard`, `UpdateLandingCard`, polling `UBoardMovementComponent::GetLandingCount`; `KiteSurf.HUD.LandingCard`).
