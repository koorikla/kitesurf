# KiteSurf Big Air Research

As of 2026-10-02. Exported from the research doc at
https://claude.ai/code/artifact/bad93cf7-dd35-4824-ac92-40f6259d1cbf

## Purpose

This doc collects what the KiteSurf project needs to know to become a big air game, and ends in a candidate backlog that Hermes can turn into tasks.

Five research agents worked in parallel, each from a different angle: physics, the real sport, game design, Unreal Engine implementation, and an audit of the current code. Sections 2 to 9 hold their findings; the candidate backlog merges their proposals and removes duplicates.

How Hermes should use it:

- Create tasks from the candidate backlog section only. The earlier sections are the evidence and the parameter values each task should cite.
- Keep each item's acceptance criterion as the task's definition of done.
- Respect the listed order: items are sorted so each one builds on what comes before it.
- Numbers carry a confidence tag: **sourced**, **typical** or **estimate**. Treat estimates as tuning starting points, not requirements.
- Anything under Open questions needs a human decision before it becomes a task.

## Big air fundamentals

A big air jump is mostly work done by the kite during the climb, not rider speed converted to height. Speed alone gives about 7 m at 12 m/s; a 35 m jump needs about 27 kJ for an 80 kg rider (**estimate**, derived). The simulation therefore has to model the send and the edge release, not a scripted jump impulse.

### Jump phases

1. **Approach:** beam reach at 10 to 14 m/s (20 to 27 kn), kite at 10:30 or 1:30 (**typical**).
2. **Edge:** progressive hard edge upwind, board heeled 50 to 65° (**estimate**).
3. **Send:** the kite is steered back through 12 in 0.7 to 1.0 s (**estimate**). Its speed adds to the apparent wind and the line tension rotates toward vertical.
4. **Pop:** the edge is released the moment tension is near vertical and above body weight; the rider sheets in.
5. **Climb:** 2 to 4 s (**estimate**).
6. **Hang:** kite overhead, sheeted in.
7. **Redirect:** the kite is flown forward, or heli-looped, 1 to 2 s before touchdown to add lift and forward pull.

What sets height: apparent wind squared (wind plus rider speed plus kite send speed), how long the edge holds while tension builds, and send timing. A late send wastes tension on horizontal pull. An early send lifts the rider off the edge at low tension. Kickers add vertical speed.

### The numbers

| Quantity | Value | Confidence |
| --- | --- | --- |
| Recreational jump | 3 to 10 m, 3 to 5 s airtime | typical |
| Advanced jump | 10 to 20 m, 5 to 8 s | typical |
| Elite jump | 20 to 35 m, 8 to 13 s | typical |
| Vertical take-off speed | 6 to 12 m/s | estimate |
| Take-off load | about 2.5 to 3 g; rear foot 14 to 16 N/kg | estimate; sourced (Lutter et al.) |
| Landing load, jumps of 1.6 to 5.6 m | mean 4.2 to 5.5 g | sourced (Simons 2025) |
| Downwind travel on an 8 to 12 s jump | 50 to 150 m | estimate |
| Share of body weight the kite carries in flight | about 67% on a 10 m jump, about 80% on a 35 m jump | estimate, derived from height and airtime |

A useful calibration check: effective gravity during a jump is `8h / t^2`. Real jumps give 1.5 to 3.5 m/s², against 9.81 in free fall.

The WOO jump sensor over-reads. A [validation study](https://pmc.ncbi.nlm.nih.gov/articles/PMC8706814/) found an error of 0.70 to 0.95 m on jumps up to 7.3 m, more than 20% above 5 m. Record heights are upper bounds.

### Kite loops

- **Why the pull is horizontal.** The kite is steered hard at or above the apex and dives through the lower power zone at 25 to 35 m/s (**estimate**). Tension rises with airspeed squared and now points sideways or down, so the rider free-falls while accelerating downwind at 2 to 4 g for about 0.5 s (**estimate**).
- **Pendulum.** The rider swings forward and up toward the kite's side on 22 m lines, and the body goes horizontal.
- **Catch.** The kite climbs the far side of the loop back to 12 on its remaining speed. A loop takes 1.5 to 2.5 s (**estimate**). Tension tilts upward again and arrests the fall.
- **Soft landing:** kite overhead and still flying forward at touchdown; sink rate 2 to 4 m/s (**estimate**).
- **Hot landing:** kite still low or in front, because the loop started too late or too low; sink 8 to 12 m/s and 15 to 20 m/s downwind (**estimate**).
- **Heli-loop:** a loop flown in the upper window during descent. Elevation stays above 60°, so it adds lift with little horizontal pull (**typical**).
- **Doobie and boogie loops** share the megaloop's kite path. The difference is rider rotation, which needs a rigid-body rider.

No measured line-tension data for kite jumps or megaloops was found. Every loop and take-off force above is derived from the lift equation.

## Kite aerodynamics and flight model

The kite should be a point mass with a heading, flying on the line sphere under lift, drag, gravity and line tension, with heading integrated from a measured turn-rate law. Four things must not be simplified away: kite velocity in the apparent wind, slack lines, the coupling between depower and steering, and steering dead time.

### Wind window and apparent wind

```
Kite position:  p = L * [cos(b)cos(a), cos(b)sin(a), sin(b)]     a = azimuth, b = elevation, x = downwind, z = up
Apparent wind:  v_a = v_wind(z) - v_kite                          v_kite includes rider velocity
Wind gradient:  v(z) = v_ref * (z / z_ref)^0.11
Crosswind:      v_a ~ v_wind * cos(a)cos(b) * sqrt(1 + (L/D)^2)   steady state
```

- A parked kite sits at about `atan(L/D)` from the downwind axis: 76 to 79° for L/D of 4 to 5 (**estimate**). The window edge is not at 90°.
- Steady-state tension at the centre of the power zone is 17 to 26 times the parked force. Surf kites on 22 m lines never reach steady state; realistic peak kite airspeed is 2 to 3 times wind speed (**estimate**).

### Lift and drag

- Measured L/D for tube kites averages about 4 powered and 3 depowered, peaking near 5 at 5 to 10° angle of attack ([Oehler and Schmehl 2019](https://wes.copernicus.org/articles/4/1/2019/), **sourced**).
- Angle of attack stays in 6 to 16° powered and -7 to +3° depowered (**sourced**, same paper).
- Steering adds drag: `C_D *= 1 + 0.6 * |steer|` ([Fechner et al.](https://arxiv.org/pdf/1406.6218), **sourced**).
- Line drag lumped at the kite adds about 0.01 to C_D and costs a surf kite about 5% of L/D (**estimate**).

Suggested lookup table. These values are recalled from the open-source Fechner kite model and were not verified this session.

| Angle of attack | -20° | -5° | 0° | 20° | 40° | 90° |
| --- | --- | --- | --- | --- | --- | --- |
| C_L | 0.08 | 0.15 | 0.2 | 1.0 | 1.0 | 0 |
| C_D | 0.2 | | 0.1 | 0.2 | | 1.0 |

### Steering

```
heading_rate = g_k * v_a * steer(t - dead_time) + (c2 / v_a) * sin(heading) * cos(b)
loop radius  ~ 1 / (g_k * steer)          3 to 10 m, independent of speed
```

- Steering gain `g_k` from [flight measurements](https://wes.copernicus.org/articles/9/2261/2024/) (**sourced**): 6 m² kite 0.21 to 0.35 rad/m; 10 m² 0.10 depowered to 0.35 powered; 13 to 14 m² 0.09 to 0.19.
- Dead time: 100 to 150 ms powered, 400 to 500 ms depowered (**sourced**). Re-check against the paper's table before treating as final.
- Gravity term `c2`: 6.28 rad·m/s² for a 10 m² kite (**sourced**, Fechner).

### Depower, stall and slack

- **Depower:** sheeting out reduces angle of attack by 12 to 20° over the bar throw (**estimate**). A depowered kite needs about 2.5 times the steering input for the same turn rate (**sourced**).
- **Backstall:** angle of attack above 20 to 25° at low airspeed; lift plateaus, drag rises, the kite falls backward (**typical**).
- **Luff:** angle of attack below about -5°; the canopy unloads and lines go slack (**typical**).
- **Overflying:** the kite's momentum or the rider swinging under it carries the kite upwind of the window edge. Tension drops to zero and the kite falls (**typical**).

### Lines and kites

- **Line length:** 20 to 24 m standard. Height records were set on 22 to 26 m lines in 2019 and on 17 m lines in 2025 (**sourced**). Short lines give faster angular travel and an earlier catch after loops.
- **Setup:** four lines, two front for power and two rear for steering. Front lines carry 70 to 90% of the load when sheeted out (**estimate**).
- **Stretch:** about 9 kN/m total on 22 m, or 1.2% at 2.5 kN (**estimate**, scaled from Fechner's tether data).
- **Kite mass** ([Cabrinha Switchblade](https://www.cabrinha.com/products/switchblade-2), **sourced**): 7 m² 2.65 kg, 9 m² 3.11 kg, 12 m² 3.79 kg, 14 m² 4.17 kg.
- **Wind range for a 75 kg rider** (**sourced**, same page): 7 m² 18 to 32 kn, 9 m² 13 to 27 kn, 12 m² 9 to 21 kn. Big air uses 7 to 9 m² in 30 to 45 kn (**typical**).

Line tension, all **estimate**, as multiples of body weight (BW):

| Situation | Tension |
| --- | --- |
| Parked at zenith, 9 m² at 30 kn, sheeted in | 1.2 BW (about 0.95 kN) |
| Same, depowered | 0.5 BW |
| Riding | 0.5 to 1.0 BW |
| Take-off | 2.5 to 4 BW |
| Megaloop peak | 3 to 5 BW |

### Parameter defaults

| Parameter | Default | Range | Confidence |
| --- | --- | --- | --- |
| Kite flat area | 9 m² | 7 to 14 | typical |
| Projected to flat area ratio | 0.72 | 0.65 to 0.80 | typical |
| Kite mass | 3.1 kg | 2.6 to 4.2 | sourced |
| Line length | 22 m | 17 to 26 | sourced |
| Peak L/D | 4.5 | 3 to 5 | sourced |
| C_L maximum | 1.0 | 0.9 to 1.2 | unverified |
| Steering drag factor | 0.6 | 0.3 to 0.8 | sourced |
| Line drag added to C_D | 0.01 | 0.005 to 0.02 | estimate |
| Steering gain, 9 m² | 0.25 rad/m | 0.09 to 0.35 | sourced |
| Gravity turn term | 6.3 rad·m/s² | 3 to 8 | sourced |
| Steering dead time | 0.15 s | 0.1 to 0.5 | sourced |
| Depower angle range | 15° | 10 to 31 | estimate |
| Depowered steering factor | 0.4 | 0.3 to 0.6 | sourced |
| Line stiffness | 9 kN/m | 5 to 15 | estimate |
| Wind speed | 30 kn | 15 to 45 | typical |
| Wind shear exponent | 0.11 | 0.08 to 0.15 | typical |
| Simulation substep | 1/240 s | 1/120 to 1/480 | estimate |

## Board, water and rider dynamics

The board's job in the model is to turn heel angle into a sideways water force that balances the kite's horizontal pull; pop is that force vanishing in about 0.1 s. Most numbers here are estimates derived from force balance.

### Edging

The water force acts along the board's normal. With heel angle `phi`, water force `N` and line tension `T`:

```
N * sin(phi) = T_horizontal
N * cos(phi) = m*g - T_vertical
tan(phi)     = T_horizontal / (m*g - T_vertical)
```

- For tension of 0.7 body weight at 35° line elevation, heel is about 44° (**estimate**).
- **Upwind limit:** the angle between apparent wind and course is at least `atan(1/LD_kite) + atan(1/LD_board)`, about 26° (**estimate**). Practical course is 15 to 25° above a beam reach (**typical**).
- **Fins** add grip at low heel; the rail does most of the work when edged. A reasonable split is 10 to 20% of side force from fins (**estimate**).

### Planing

- **Twin-tip:** 135 to 142 cm by 40 to 42 cm, 2.5 to 3.5 kg, rocker 3 to 5 cm, four fins of 4 to 5 cm (**typical**).
- **Speed:** 15 to 25 kn typical, up to 35 kn (**typical**).
- **Trim and wetted length:** at 10 m/s carrying 900 N, trim angle is 6 to 10° and wetted length 0.2 to 0.6 m (**estimate**, from the Savitsky planing formula recalled from memory).
- **Drag:** about 135 N at that point, so board L/D is 4 to 7 flat and 3 to 5 edged (**estimate**).

### Pop

- The sideways water force drops to zero within about 0.1 s as the board flattens and leaves the water. The unbalanced tension, already above body weight and near vertical, accelerates the rider upward.
- Leg extension adds 1 to 2 m/s (**estimate**).
- Line stretch stores about 330 J, worth about 0.4 m of height (**estimate**).

### Landing

- Measured mean landing load is 4.2 to 5.5 g on jumps of 1.6 to 5.6 m, with peak foot force of 31.7 N/kg (**sourced**, Simons 2025 abstract).
- Failed loops raise landing load because the kite is no longer acting as a parachute (**sourced**, same study).

### Rider

| Parameter | Default | Range | Confidence |
| --- | --- | --- | --- |
| Rider mass | 78 kg, plus 3 kg board | 60 to 95 | typical |
| Harness hook height above feet | 1.0 m | | estimate |
| Hook offset in front of centre of mass | 0.1 to 0.15 m | | estimate |
| Lean from vertical when riding | | 30 to 60° | typical |
| Body drag area (CdA) | 0.7 m² | 0.5 to 1.0 | estimate |
| Board beam and length | 0.41 m, 1.38 m | 0.38 to 0.45, 1.32 to 1.45 | typical |
| Board L/D edged | 4 | 3 to 6 | estimate |
| Maximum heel angle | 65° | 50 to 75 | estimate |

For a game, use a point mass for the trajectory plus a rigid body for rotations, with line tension applied at the harness hook. Rotation tricks need the rigid body; height and airtime do not.

## Wind and sea conditions

Record big air needs 35 to 45 kn or more, plus either a steep kicker (Cape Town) or flat water with a strong gust (Le Barcarès). The game's spots should differ in exactly those terms: wind strength, direction to the shore, gustiness and water state.

### Spots

| Spot | Wind | Direction to shore | Gustiness | Water |
| --- | --- | --- | --- | --- |
| Kite Beach, Blouberg (Cape Town) | South-easter, 25 to 40 kn, peaks in the afternoon, October to March | Side to cross-onshore | Moderate | Ocean swell with clean kickers |
| Misty Cliffs (Cape) | Gale-force south-south-east | Varies | Erratic | Very big kickers, rocks, strong current |
| Tarifa | Levante, 20 to 50 kn | Offshore to side-off | Very gusty | Flat inside, chop outside |
| Tatajuba, Brazil | 20 to 30 kn, very consistent | Side-onshore | Steady | Flat lagoons; a training ground |
| Dakhla | 25 to 35 kn in summer | Offshore | Fairly steady | Very flat at low tide |
| Leucate and Le Barcarès | Tramontane, 30 to 45 kn or more | Offshore | Gusts 10 to 15 kn over the average | Flat lagoon, cold |
| Maui (Kanaha) | Trade winds, 15 to 25 kn | Side-onshore | Steady | Reef waves; usually too light for records |

Only the Cape Town and Le Barcarès wind figures come from pages that were opened; the other rows are from search summaries of spot guides. Riders at the Le Barcarès lagoon [reportedly jump 3 to 5 m higher](https://www.gkakiteworldtour.com/green-light-for-lords-of-tram-2026/) than elsewhere. [King of the Air](https://www.redbull.com/ca-en/red-bull-king-of-the-air-wind-conditions-explained) is rarely run under a 25 kn average and wants a forecast rising to 35 to 37 kn with swell for kickers.

### What the wind model needs

The current `UWindComponent` has smooth Perlin gusts of ±30% on a 15 kn base, ±10° of direction drift and a log shear profile. For big air it needs:

- **Base wind of 25 to 45 kn**, set per spot. The 15 kn default is below the big air range.
- **Discrete gusts and lulls** that travel downwind at the mean wind speed, so the player can see one coming and time a send on it.
- **Gust strength per spot:** steady at a flat lagoon, 10 to 15 kn over the average at a Tramontane spot.
- **Shear:** speed proportional to height to the power 0.11 (**typical**), so the kite at 20 m sees more wind than the rider.
- **One world-level field** sampled by gameplay, water darkening, spray and audio alike.

### What the water needs

- **Waves in the board query.** The board currently rides the flat rest surface.
- **Kickers:** a few long, steep swell faces aligned to the wind, ridden straight out to. Take-off from a kicker face adds vertical speed.
- **Chop:** short random waves that disturb the edge and the landing.
- **Flat-water spots** with no kickers, where gust timing is the whole game.
- **Visible gusts:** dark bands on the water at least 3 s before the gust arrives.

## Tricks, competition and scoring

Real big air is judged on four things: height, extremity, technicality and execution, with variety across a heat. A game trick is a composition of kite path (loop count and direction), rider rotation and board state, so the trick system should detect those parts and name the combination.

### Trick catalogue

Names follow the [GKA big air trick list](https://www.gkakiteworldtour.com/wp-content/uploads/2023/04/Tricklist-Big-Air-TT-2023.pdf) and [North's](https://northactionsports.com/blogs/all/big-air-tricks-explained) [explainers](https://northactionsports.com/blogs/all/extreme-big-air-tricks-explained-by-colin-carroll). Difficulty (1 to 5) and prerequisites are editorial estimates, not an official scale.

| Trick | What kite and rider do | Difficulty | Prerequisite |
| --- | --- | --- | --- |
| Sent jump | Edge hard, steer the kite up to 12, pop, sheet in, redirect forward to land | 1 | Riding upwind |
| Grab | One hand off the bar to grab the board | 1 | Sent jump |
| Back roll, front roll | Body rotates backward or forward; kite parked near 12 | 2 | Sent jump |
| Heli loop landing | Kite spins a tight loop overhead during descent to brake and pull forward | 2 | Sent jump above 5 m |
| One-footer | One foot out of the strap | 2 | Grab |
| Board-off | Board taken off both feet, held, replaced before landing | 3 | Grab, jumps above 8 m |
| Deadman | Rider hangs inverted, hands off the bar | 3 | Board-off level control |
| Kite loop | Kite pulled through a full circle; a low loop pulls horizontally | 3 | Heli loop, back roll |
| Megaloop | At height, the kite circles through the power zone level with or below the rider; a big sideways yank, free fall, then the catch | 4 | Kite loop, jumps above 10 m |
| Megaloop back roll | Megaloop with a back roll; the late version starts the roll after the yank | 4 | Megaloop, back roll |
| Megaloop board-off | Board removed during or after the loop | 4 | Megaloop, board-off |
| Boogie loop | Kite loop with one inverted front roll | 4 | Megaloop, front roll |
| Contra loop | Kite looped the opposite way round; harder free fall afterwards | 5 | Megaloop |
| Doobie loop | Boogie loop with two inverted front rolls | 5 | Boogie loop |
| Double or triple loop | Two or three kite loops in one jump; needs a small fast kite | 5 | Megaloop, jumps above 15 m |
| S-loop | Half a megaloop, then the kite is driven back the other way | 5 | Megaloop, contra loop |

Progression: sent jump, then grabs and rotations, then heli loop landing, then board-off and kite loop, then megaloop, then megaloop with a rotation or board-off, then boogie and contra, then doubles, S-loop and doobie.

### Competition formats

**Red Bull King of the Air** (Kite Beach, Cape Town):

- Each trick scores 0 to 10 on height, extremity, technicality and execution.
- The heat score is the best three tricks plus one overall impression score, each worth 25% ([Mystic](https://www.mysticboarding.com/blogs/news/how-to-qualify-for-red-bull-king-of-the-air), [North](https://northactionsports.com/blogs/all/judging-criteria-explained), 2024).
- Crashes do not score.
- 2025 format, from search summaries only: round one is 13-minute heats of three riders, with the lowest scorer flagged out at 8 minutes.
- The 2025 winner was [Lorenzo Casati](https://www.sail-world.com/news/291987/Lorenzo-wins-Red-Bull-King-of-the-Air-Family-Final) with 34.02 points. The 2026 window is 21 November to 6 December 2026.

**GKA Big Air World Tour** ([2025 rulebook](https://www.gkakiteworldtour.com/rulebooks/2025/GKA%20RULEBOOK%202025.pdf)):

- Tricks score 0.1 to 10.0. Criteria are height, extremity (yank, kite angle, speed in and out), technical difficulty, landing, smoothness and innovation.
- A repeated or evolved version of the same trick counts once.
- Scores of 8 to 10 need a trick that is very high, very extreme and very technical. High but not extreme, or extreme but low, scores poorly.
- In wind above 40 kn, judges favour double loops and S-loops over technical tricks that lose height.

**Red Bull Megaloop** (Netherlands): one-against-one 8-minute heats, and men's lines capped at 18 m in 2025.

### Measurement and records

- The [WOO sensor](https://docs.woosports.com/docs/kite-big-air) records height, airtime and landing g per jump, plus session totals. It does not detect tricks.
- **Current record:** [Jamie Overbeek, 42.3 m](https://bb-talkin.eu/blogs/news/jamie-overbeek-bbtalkin-42-3m-record-story), Le Barcarès lagoon, April 2026, Tramontane above 40 kn, on a twin-tip with a five-strut kite. Airtime was over 12 s and he landed with a heli loop.
- **Before that:** [Hugo Wigglesworth, 40.0 m](https://www.iksurfmag.com/kitesurfing-news/2025/12/hugo-wigglesworths-40-meter-kite-jump-a-new-world-record/), Cape Town, 6 December 2025, on a hydrofoil board with 17 m lines. Foil and twin-tip jumps are best kept as separate categories.
- **Typical heights** (search summaries): beginners 3 to 5 m, average riders 5 to 7 m, 10 m is a milestone, 15 to 20 m is advanced, above 30 m is elite.

### Gear that changes behaviour

| Choice | Options | Effect |
| --- | --- | --- |
| Kite type | Five-strut boost kite or three-strut loop kite | Boost: more lift and hangtime, slower turning. Loop: pivots faster and climbs back sooner to catch the rider, less glide |
| Kite size | 9 m² at 35 kn, 7 to 8 m² above that, 6 to 7 m² for double loops | Smaller is faster and easier to hold in strong wind |
| Line length | 22 to 24 m, 20 m, or 12 to 18 m | Long: more height and float. Short: faster, lower, more horizontal loops, and needs 40 kn or more |
| Board | Twin-tip 132 to 138 cm with straps, or boots | Straps allow board-offs; boots hold a harder edge but rule board-offs out |

### Vocabulary for tutorials and failure messages

- **Loading:** carving upwind to build line tension. **Sending:** steering the kite up toward 12. **Pop:** releasing the edge. **Yank:** the horizontal pull of a loop. **Catch:** the kite climbing back overhead before impact.
- **Crashes:** hindenburg (kite stalls and falls), lofting (a gust lifts the rider involuntarily), tea-bagging (repeatedly dunked and lifted), death loop (kite loops uncontrolled), over-rotation, losing the board on a board-off.

### What the game should reward

- Height shown as a live number.
- Kite angle: the kite at or below the rider, a visible yank and long horizontal travel.
- Late, low loops and the suspense of the catch.
- Stacked technical tricks at height, and variety across a heat.
- Clean ride-aways with speed.

## Game design

The game should be built around one repeatable 30 to 60 second loop: ride out, read the water, send the kite, fly, land, turn. Everything that is tedious in real kiting is cut. Tuning numbers in this section are design proposals (**estimate**), not sourced values.

### Core loop

1. **Ride out:** edge, park the kite at 10 or 2 o'clock, build speed for 5 to 10 s.
2. **Read:** spot a kicker and an incoming gust line on the water.
3. **Send:** steer the kite hard back through 12, load the edge, pop, sheet in.
4. **Fly:** hold or loop the kite; add rotations, grabs and board-offs.
5. **Land:** redirect or heli-loop the kite for a soft touchdown, pointing downwind.
6. **Turn and repeat** on the other tack.

Tedium to cut:

- **Staying upwind:** hidden drift compensation, plus a one-button reset upwind with no penalty in free ride.
- **Relaunch:** automatic within 2 s of a crash.
- **Waiting for gusts:** gusts arrive every 10 to 20 s and show as dark bands on the water, so waiting becomes timing.
- **Rigging:** kite size is a menu choice.

Existing kite games are few and small. [Kiteboarding Pro](https://store.steampowered.com/app/1629300/Kiteboarding_Pro/) has 16 Steam reviews and its own store page warns that controlling kite and board together is tricky. [Winds Up Kitesurfing](https://store.steampowered.com/app/2799240/Winds_Up_Kitesurfing/) has 3 reviews and [Kitesurf Runner](https://store.steampowered.com/app/3025260/Kitesurf_Runner/) has 1. Nobody has solved the control problem, and the game must read clearly to non-kiters.

### Control schemes

The left hand rides the board and the right hand flies the kite. Sheeting sits on an analog trigger because bar throw is a one-axis analog pull. Sim-cade is the default; the fully physical mapping is the top tier, never the default.

| Input | Arcade | Sim-cade (default) | Simulation |
| --- | --- | --- | --- |
| Left stick X | Heading | Heading and carve | Board yaw and carve |
| Left stick Y | Not used | Edge harder or flatten | Edge pressure and fore-aft weight |
| Right stick X | Loop direction in the air | Kite steering as a rate, holds position on release | Bar steering, direct, self-centring |
| Right stick Y | Not used | Nudge the parked kite height | Not used |
| Right trigger | Hold to charge, release to jump | Sheet in | Sheet in, 1:1 bar throw |
| Left trigger | Depower | Load edge; release to pop | Load edge; release to pop |
| Right stick gesture in the air | Flick = kiteloop | Half-circle = kiteloop; hold over = heli-loop | Loops only by steering the bar |
| Left stick in the air | Rotations | Rotations | Rotations |
| Bumpers | Grabs | Grabs; both = board-off | Grabs; both = board-off |
| Assists on | All | Auto-park, soft auto-redirect, upwind assist | None |

Keyboard and mouse: A/D heading, W/S edge, mouse X sets a target clock position for the kite, right mouse sheets in with a 150 ms ramp, Space loads and pops, Q/E grabs.

Assists, each an independent toggle:

- **Auto-edge:** the board holds the edge angle needed to keep line tension.
- **Auto-park:** with the stick released, the kite returns to its last commanded clock position.
- **Auto-send:** one button runs the send-and-pop sequence.
- **Auto-redirect:** below about 4 m on descent, the kite is flown forward and overhead to cushion the landing.
- **Loop catch:** after a loop the kite always climbs back to 12.
- **Upwind assist:** hidden drift compensation.

A loop should be a gesture whose radius and speed set the loop's tightness: a tight fast circle gives a hard low loop, a slow one a lazy heli-loop. The input shape is the trick, so no canned loop animations are needed.

### Camera

- **Riding:** 5 to 6 m behind and 2 m up, offset upwind so the kite sits in the upper leeward third. FOV 75 degrees, rising to 90 with speed.
- **In the air:** treat rider and kite as a pair and dolly back to keep both inside an 80% safe frame. If both cannot fit, the rider wins and the kite gets an edge-of-screen marker.
- **Height:** keep water and a reference object in the bottom third of the frame; height only reads against a reference.
- **Landing:** from the apex, swing above and behind the rider and project a landing decal on the water.
- **Alternates:** kite-cam looking down the lines, drone orbit above a height threshold, beach long-lens. Line-mounted views are for replay, not play.
- **Replay:** buffer the last 20 s and offer replay of the last jump on one button, with scrubbing and the camera presets above.

### Feel and feedback

- **Line tension:** continuous low rumble scaled to line load, plus a line hum whose pitch rises with tension. This is the most important audio cue.
- **Triggers:** right trigger resistance proportional to bar pressure on a DualSense, with rumble as the fallback.
- **Apparent wind:** wind noise from apparent wind speed, low-passed at the apex for a quiet moment.
- **Board:** spray and rooster tail scaled to edge load.
- **Pop:** 2 to 3 frames of hitstop and a bass thump on a well-timed pop.
- **Apex:** optional 0.6x slow motion for 0.5 s on a personal best, with a height callout.
- **Landing:** flash and rumble scaled to impact, with the landing g shown.

### HUD

Four elements by default; everything else is contextual.

1. **Wind-window arc** (exists): add a ghost marker for the commanded kite position and colour the power zone.
2. **Power gauge:** one vertical bar for line tension.
3. **Wind on the water:** a direction chevron under the rider, visible gust bands, and a pulse 2 s before a gust hits.
4. **Jump card:** shown from take-off until 3 s after landing, with height, airtime, landing g and a landing grade. These are the metrics the [WOO device](https://docs.woosports.com/docs/kite-big-air) reports.

### Scoring and progression

Proposed per-jump score: `Height x (1 + Extremity) x (1 + Technicality) x Execution`.

- **Height:** metres, slightly superlinear.
- **Extremity:** how low the kite gets in the loop relative to the rider, plus horizontal travel and loop count.
- **Technicality:** rotations, grabs, board-offs.
- **Execution:** 0.3 to 1.0 from landing g, board angle and whether the rider rode away.
- **Heat score:** best three jumps plus a variety bonus; repeated tricks are devalued. No combo meter, because big air is about discrete sends.

Progression:

- **Session challenges:** three per spot visit, such as a 15 m jump or a kiteloop landed under 3 g.
- **Spots unlock by wind character:** flat lagoon at 20 kn, choppy bay at 28 kn, wave kickers at 35 kn, storm at 45 kn or more.
- **Gear changes behaviour:** kite size, kite type (boost against loop), line length, board.
- **Trick book:** a trick is unlocked by landing it once in a guided challenge.
- **Daily wind:** one seeded condition per spot per day with a leaderboard and downloadable ghosts.

### Onboarding and failure

1. **Minute 0 to 1:** kite only, standing on the beach; fly the kite to targets on the arc and feel the pull grow.
2. **Minute 1 to 3:** on the water with auto-edge; ride through gates and turn once.
3. **Minute 3 to 6:** first jump with time slowed as the kite passes 12 and a release prompt; a 5 m jump is guaranteed.
4. **Minute 6 to 10:** assists peel off one at a time, ending with a first kiteloop.

A crash gives at most 1.5 s of ragdoll, then an instant reset on the same tack with speed. After every failed jump, show a one-line cause such as popped late or kite too low. A settable session marker lets the player teleport back to a chosen place.

### Scope for a small team

| Milestone | Contents | Exit test |
| --- | --- | --- |
| Vertical slice | One spot, one gusty wind, one kite, sim-cade controls with three assists, jump, kiteloop, one rotation, one grab, two-target camera, jump card, tension rumble and hum, instant reset, 90 s best-three session | A non-kiter lands a jump above 10 m within 10 minutes and chooses to play again |
| Depth | Arcade and sim presets, keyboard and mouse, onboarding, full score model, replay and photo mode, three more tricks, second spot, kite sizes | |
| Retention | Tour and heats, challenges, gear unlocks, daily wind with leaderboards and ghosts, accessibility suite, four spots | |

Where realism yields to fun: both tacks are equally easy, upwind drift is compensated, lulls never drop the kite in arcade or sim-cade, and loop catch is on by default. The physics stays honest where it creates skill: timing of the send, kite position in the window, and landing with the kite overhead.

## UE5 implementation notes

Keep the simulation as custom code on a fixed 240 Hz step with a constraint-based tether, and keep it free of rendering so tests can call it directly. Class and cvar names below were checked against the installed 5.8.3 engine source unless listed as unverified under Open questions.

| Area | Do first | Avoid | Defer |
| --- | --- | --- | --- |
| Physics | Fixed-step accumulator, XPBD tether, render interpolation | Chaos rigid bodies for rider or kite; async physics tick | |
| Lines | Analytic visual-only line renderer in a late tick | `UCableComponent` for the flying lines | Per-segment line simulation |
| Water | Own water-surface interface, 3 to 5 sample points under the board, Niagara spray | `UBuoyancyComponent` for the board | Shallow-water sim, FFT ocean patch |
| Wind | One world-level seeded wind field, water darkening for gusts | Engine wind source as the gameplay source | Wind shadow |
| Rider | Authored poses plus Control Rig IK on a mannequin | Motion Matching | Trick animation, ragdoll |
| Kite | Skeletal mesh with a few bones, shader flutter | Chaos Cloth, Nanite | Water relaunch |
| Camera | Custom two-target framing camera | Gameplay Cameras plugin (experimental) | |
| Audio | MetaSound bed driven by three parameters | | Mix states |
| Replay | Own input-and-seed recorder with a transform track | Engine replay system | Sequencer capture |
| Tests | Golden trajectories under `-nullrhi` | GPU tests in the blocking job | Low-Level Tests, Gauntlet |

### Physics architecture

- **Custom integration, not Chaos.** The system is two point masses and a tether with lookup-table forces. Chaos adds thread marshalling and gives nothing back until collisions matter.
- **One fixed step.** The pawn or a world subsystem owns `Step(dt)` at 240 Hz or more and calls it N times per frame. Store previous and current state and interpolate for rendering.
- **Tether.** A 24 m Dyneema line is effectively inextensible; a spring at real stiffness blows up explicit Euler at 60 Hz. Use semi-implicit Euler for free motion plus a distance constraint that is active only when taut, solved as XPBD with one iteration per substep.
- **Tension for free.** Derive line tension from the constraint's Lagrange multiplier. That one signal feeds HUD, audio, rumble and landing g.
- **Determinism.** Record inputs plus the wind seed and re-simulate at the fixed step. Float results are not guaranteed to match across machines, so shared ghosts should use recorded transforms.
- **Engine alternatives exist** (`AsyncPhysicsTickComponent`, `bTickPhysicsAsync`), but async physics and sub-stepping are mutually exclusive and game-thread objects lag behind the async tick.

### Kite lines

- Compute each of the four lines analytically per frame from the bar end and the kite attach point: a straight segment plus sag that grows as tension drops, plus a small bow from wind drag.
- Draw them as a Niagara ribbon with 8 to 16 points per line, or one dynamic mesh strip.
- A 2 mm line is sub-pixel beyond a few metres. Clamp its width in screen space and fade it with distance.
- Update line endpoints after the simulation and after the camera (`TG_PostUpdateWork`) to avoid jitter.

### Water

- **Plugin status:** `Water`, `WaterAdvanced` and `Buoyancy` are all marked experimental in 5.8.3.
- **Query to use:** `UWaterBodyComponent::TryQueryWaterInfoClosestToWorldLocation` with `EWaterBodyQueryFlags::IncludeWaves`. The current code's call leaves waves out.
- **Abstraction:** put water behind a project interface with two implementations: the Water plugin, and a pure analytic Gerstner or flat surface for tests.
- **Under the board:** sample nose, tail, both rails and centre. Fit a plane for the normal and use relative vertical velocity for slam and pop. The default generator has 16 waves, so this is cheap.
- **Kickers:** Gerstner waves cannot break. Author rideable kickers as a few long, steep swell trains aligned to the wind, or as an analytic kicker wave added to both the query and the water material.
- **Spray and wake:** start with plain Niagara driven by speed and edge angle, plus a foam ribbon.
- **Vulkan artifact:** Epic staff [confirmed Single Layer Water artifacts on Vulkan SM6](https://forums.unrealengine.com/t/vulkan-and-dx12-parity-singlelayerwater-issues/2720975) for 5.6 and 5.7. The workaround is `r.Water.SingleLayer.DepthPrepass=0`. Whether 5.8.3 still needs it is unverified.

### Wind

- One `UWorldSubsystem` with a pure `Sample(Position, Time)` function: mean wind, shear with altitude, and seeded gust cells that drift downwind at the mean speed.
- Feed the same field to everything: a Material Parameter Collection and a low-resolution gust texture for water darkening and flags, Niagara parameters for streaks, and audio parameters.
- Mirror the mean wind into the engine's `UWindDirectionalSourceComponent` so cloth and foliage respond.

### Rider, kite and camera

- **Rider:** a small set of authored poses (heelside, toeside, edge-hard, sent, airborne tuck, landing absorb) blended by edge angle and load. Control Rig locks feet to the board and hands to the bar, and leans the spine along the tension direction.
- **Crashes:** `UPhysicalAnimationComponent` or the Physics Control plugin for a blended ragdoll, with a soft constraint from pelvis to chicken loop so the kite drags the body.
- **Kite:** a skeletal mesh with a few bones for span bend and tip twist. Canopy flutter is a vertex shader driven by a luff parameter and apparent wind speed.
- **Camera:** a custom camera component or a `APlayerCameraManager::UpdateViewTarget` override. The look-at point blends rider and kite, with the kite's weight rising with jump height. The spring arm's lag is frame-rate sensitive and it cannot frame two targets.

### Audio, replay and telemetry

- **Audio:** MetaSounds driven by `ApparentWind`, `LineTension`, `BoardSpeed`, `EdgeLoad` and `Airborne`, with one-shots for pop, landing and kite-loop whoosh.
- **Replay:** a project recorder storing input, seed and a 30 Hz transform track. It serves ghosts, instant replay and golden tests. The engine replay system records replicated state and is heavy for single player.
- **Telemetry:** one jump record per jump (take-off speed, peak height above local water, airtime, loop count, peak tension, landing g, landing board angle), emitted as a delegate and as CSV under `Saved/Telemetry`.

### Testing and CI

- Keep `-nullrhi` as the blocking gate.
- **Golden-trajectory tests:** fixed step, fixed seed, scripted inputs, compared against stored trajectories with tolerances. Also check invariants: line length never exceeds its limit, energy stays bounded, no NaN. Run at 60, 120 and 240 Hz to assert frame-rate independence.
- **GPU tests** carry `EAutomationTestFlags::NonNullRHI` and run in a separate, non-blocking job.
- **Device loss on Linux:** forum threads for 5.8.0 report `VK_ERROR_DEVICE_LOST` on [NVIDIA](https://forums.unrealengine.com/t/ue-5-8-release-instant-vulkan-crash-vk-error-device-lost-on-linux-with-rtx-3090-ti-nvidia-driver/2729632) and [AMD](https://forums.unrealengine.com/t/ue-5-8-vulkan-on-linux-triggers-vk-error-device-lost-gpuvm-read-fault-on-amd-rx-9070-xt-with-radv/2730749). Reported mitigations are `-NoRaytracing`, `r.Vulkan.AllowAsyncCompute=0` and `r.RDG.AsyncCompute=0`. The selection-outline pass is editor-only, so GPU tests should run with `-game`.

### Performance

- An ocean scene has few instances, so ray tracing structure cost is low. The cost is water reflections, volumetric clouds and sky, since the camera looks up at the kite.
- Scalability settings worth exposing: `r.Water.SingleLayer.Reflection`, its `DownsampleFactor`, `r.Water.SingleLayer.RefractionDownsampleFactor`, water mesh tessellation, hardware against software Lumen, cloud quality, shadow quality and dynamic resolution.
- Use the water mesh's far-distance material for the horizon instead of a huge water zone.
- Measure a GPU budget with `stat gpu` on the RTX 5060 early.

Multiplayer is feasible later through custom replicated state or `UNetworkPhysicsComponent`. Both need the fixed-step, input-driven simulation first.

## Current codebase

The project today is a riding prototype: a kite that slides sideways on a fixed-length line, a point-mass board, and an analytic wind field. It cannot fly a kite loop, has no jump or landing logic, and has no meshes at all. The audit read `main` at `1702a53`; paths are under `Source/KiteSurf/Private/`. Items marked *inferred* were reasoned from the code, not observed in a run.

### What exists

| Class | What it does today | Key defaults |
| --- | --- | --- |
| `UWindComponent` | Stateless analytic wind: Perlin gusts, direction drift, log shear with height | 15 kn base, gusts ±30%, period 8 s, drift ±10°, full speed at 10 m |
| `UKiteWindMath` | Unit conversion and wind-window geometry helpers | 1 kn = 51.44 cm/s |
| `UKiteComponent` | Kite position from azimuth and elevation on a rigid line; lift and drag; line tension | 12 m², 24 m lines, steer 10 deg/s per m/s |
| `UBoardMovementComponent` | Point mass with gravity, buoyancy spring, displacement and planing drag, edging, carve yaw | 85 kg, planing above 4 m/s, max edge 45° |
| `AKiteRiderPawn` | Owns the components, binds input, passes line force to the board | Spring arm 8 m |
| `AKiteSurfHUD` | Canvas HUD: speed, wind, wind-window arc (azimuth only), FPS | |

- **Size:** one module, about 1,100 lines of C++ and 660 lines of tests.
- **Input:** three 1D axes only: steer (A/D, left stick X), sheet (W, right trigger), edge (Q/E, left stick Y). No pop, loop, trick, grab, reset or pause action.
- **Content:** one generated map (ocean, sky, clouds, fog, player start), three Blueprints and four input assets. No meshes, materials, animations, audio or Niagara systems.
- **Tests:** 17 automation tests on the components in isolation. None covers the coupled kite-and-board system, a trajectory, or anything airborne.
- **CI:** self-hosted runner builds the editor target and runs the tests with `-nullrhi`. No game-target build, packaging or rendering smoke test.

### How the models work

**Kite** (`KiteComponent.cpp:151-277`). State is azimuth, elevation, sheet and steer. The kite sits exactly on the line sphere; lines never slack and the kite has no mass, heading or velocity state.

```
DeltaAzimuth = Steer * SteerSensitivity * ApparentWindSpeed * dt     (azimuth clamped to +-90 deg)
Alpha = Lerp(4 deg, 18 deg, Sheet)
Cl = Clamp(2*PI*sin(Alpha), 0, 1.2)      Cd = 0.05 + Cl^2 / (PI*5)
LineTension = max(0, (Lift + Drag) . LineDir)
```

**Board** (`BoardMovementComponent.cpp:114-231`). One 85 kg point mass, semi-implicit Euler at the variable frame step.

```
Buoyancy = Clamp(m*g + 3000*Submersion - 800*Vz, 0, 1500 N)
Forward drag = 0.1*v^2 below 4 m/s, 40*v above
Planing lift = 50*(speed - 4 m/s)
Lateral force = (30 + 400*|Edge|) * LateralSpeed
```

Orientation is set directly: pitch and roll follow the water normal plus up to 45° of edge roll, and yaw carves with edge.

### Gaps for big air, in priority order

1. **Kite flight dynamics.** Loops are impossible: steering moves azimuth only, elevation can only rise, and azimuth is clamped. Needs a velocity state on the line sphere and a turn rate from bar input.
2. **Apparent wind from kite motion.** The force uses wind computed before the kite moves, so sweeping the kite adds no power. The power zone is currently the weakest part of the window, which is backwards.
3. **Airborne state machine.** No riding, loaded, airborne, landing or crashed states. Water forces are not gated on contact.
4. **Pop and edge loading.** One edge axis drives roll, grip and yaw together, so an edge cannot be held against the kite without turning.
5. **Line tension and slack.** Tension is a projection of a static force; it needs a real constraint, slack lines and a HUD readout.
6. **Sheeting and depower.** Sheet saturates halfway and resets on key release. Needs a persistent bar position and a lift curve with stall.
7. **Landing and crash detection.** None; buoyancy is capped at 1,500 N.
8. **Jump telemetry.** Height, airtime, peak and landing g, distance.
9. **Tricks and scoring.** No rotation in the air, no grab inputs, no score state.
10. **Waves and kickers.** The water query excludes waves, so the board rides the flat rest surface.
11. **Visuals.** No rider, board or kite mesh; lines are a debug draw only.
12. **Camera.** The chase camera now turns and tilts towards the kite to keep it in frame while riding (`KiteSurf.Pawn.CameraFramesKite`); it still cannot frame a kite at the zenith or a 20 m jump.
13. **Tuning data assets.** Every constant is a constructor default or an inline literal.
14. **Physics regression tests.** Variable-step integration makes results frame-rate dependent; no golden trajectories.
15. **World-level wind.** The wind field lives on the pawn, so each pawn would have its own wind.
16. **Audio, VFX, replay.** None; replay needs a deterministic fixed step first.

### Bugs and dubious physics found

- **Kite elevation only goes up** (fixed): steering now flies the kite around the window by clock position and it holds where it is put; see `KiteSurf.Kite.FliesAroundTheWindow`.
- **Sheet does nothing above 0.5** (mostly fixed): angle of attack now runs 1.5° to 13°, so the lift clamp is reached at about 80% bar travel, and the bar position persists.
- **Water forces act in the air.** Planing lift, drag and carve yaw are not gated on contact (`BoardMovementComponent.cpp:170-217`).
- **Waves are ignored.** `GetWaterSurfaceInfoAtLocation` is called without the include-waves flag (`BoardMovementComponent.cpp:56-83`).
- **Buoyancy is a force cap** with a jump of about 533 N at the 10 cm cut-off (`BoardMovementComponent.cpp:140-148`).
- **Jumps would never end** (*inferred*): the kite is rigidly overhead with no gravity, so above about 19 kn the rider lifts off and keeps climbing.
- **Landings would sink the board** (*inferred*): the 1,500 N cap gives about 0.8 g of deceleration, so a 10 m drop sinks about 12 m.
- **HUD wind reads low** (fixed): the HUD samples the wind at the shear reference height.
- **Wind window follows gust drift.** The downwind axis is resampled every frame, so the kite shifts sideways with direction noise (`KiteComponent.cpp:135-136`).
- **Line force is one frame late** (*inferred*): the movement component ticks before the pawn that feeds it.
- **Camera rolls with the board** (fixed): the spring arm now takes only the board's heading; see `KiteSurf.Pawn.CameraStaysLevel`.
- **CI can pass without running tests.** `scripts/parse_test_report.py:17-18` swallows exceptions, and `scripts/run-tests.sh:16-17` only warns when the report is missing.
- **One test asserts nothing.** `KiteRiderPawnTests.cpp:24` is `TestTrue(true)`.
- **Mixed units.** Kite aero is in SI and converted at the boundary; board coefficients are in unlabelled cm-based units. The knots constant is repeated in three places.
- **Stale docs.** `README.md` and `docs/ARCHITECTURE.md` name things that no longer exist or behave differently.
- **Committed token.** The Android file-server `SecurityToken` is in `Config/DefaultEngine.ini`.

## Candidate backlog

There are 55 candidate tasks in seven epics, merged from the five agents' proposals. Epics are in build order: foundations, then the kite, then jumping and landing, then controls, presentation, scoring and finally long-term content. Sizes are S (under a day), M (a few days) and L (a week or more) and are rough. Milestone says which of the three scope stages a task belongs to: Slice, Depth or Retention.

### A. Foundations

| ID | Task | Acceptance criterion | Size | Depends on | Milestone |
| --- | --- | --- | --- | --- | --- |
| A1 | Make CI fail when tests do not run | A missing or unparseable test report fails the job; the placeholder pawn test asserts real behaviour | S | | Slice |
| A2 | Fixed-step simulation core with render interpolation | The same scripted input gives the same state within 0.001 cm at 30, 60 and 144 fps | M | | Slice |
| A3 | Tuning data assets and one unit convention | Kite, board and wind presets load from data assets; SI units inside the simulation; the knots constant exists once | M | | Slice |
| A4 | World-level wind field with seeded travelling gusts and shear | Deterministic for a seed; mean and variance unit-tested; kite, HUD and effects sample the same field; HUD wind is reported at 10 m | M | A2 | Slice |
| A5 | Water-surface interface with Water plugin and analytic implementations | Both return height and normal within 2 cm at 100 sample points; the plugin implementation includes waves | M | | Slice |
| A6 | Golden-trajectory regression tests | Three scripted scenarios pass at 60, 120 and 240 Hz in the `-nullrhi` job; invariants checked: line length, bounded energy, no NaN | M | A2 | Slice |
| A7 | Non-blocking GPU smoke job | 600 frames with `-game -RenderOffScreen` and no device loss; log scanned for errors | S | | Slice |
| A8 | Refresh stale docs | `README.md` and `docs/ARCHITECTURE.md` name only things that exist and describe current behaviour | S | | Slice |

### B. Kite flight model

| ID | Task | Acceptance criterion | Size | Depends on | Milestone |
| --- | --- | --- | --- | --- | --- |
| B1 | Kite as a point mass with velocity on the line sphere, with its own motion in the apparent wind | A parked kite settles within 2° of `atan(L/D)` from downwind; tension in the power zone exceeds tension at zenith | L | A2, A4 | Slice |
| B2 | Lift and drag lookup tables with stall | Zenith tension for 9 m² at 30 kn is 0.9 to 1.0 kN sheeted in and under 0.45 kN sheeted out | M | B1 | Slice |
| B3 | Turn-rate steering law with dead time and gravity term | At full bar the kite flies a complete loop of radius `1/g_k` ±15% at both 15 and 30 m/s airspeed | M | B1 | Slice |
| B4 | Persistent sheeting and depower | Power changes monotonically over the whole trigger range; turn rate at full depower is 35 to 45% of powered | M | B2, B3 | Slice |
| B5 | Constraint-based tether with taut and slack states | Line length never exceeds its limit by more than 1 cm in a 60 s loop test; tension is never negative and is reported in newtons; stable at 60 fps | M | B1 | Slice |
| B6 | Stall, luff and overfly behaviour | Oversheeting under 10 kn backstalls the kite; a kite pushed past the window edge luffs and recovers or falls within 3 s | M | B2, B5 | Depth |
| B7 | Kite types, sizes and line lengths as presets | A boost kite reaches a higher apex and a loop kite a shorter loop time for the same input; shorter lines reduce apex height and loop duration | M | A3, B3 | Depth |

### C. Board, jump and landing

| ID | Task | Acceptance criterion | Size | Depends on | Milestone |
| --- | --- | --- | --- | --- | --- |
| C1 | Rider state machine: riding, loaded, airborne, landing, crashed | Water forces apply only in contact; no planing lift or carve while airborne | M | A2 | Slice |
| C2 | Edging from force balance, with edge separated from heading | On a beam reach in 25 kn the rider holds 18 to 25 kn and points 15 to 25° upwind; a flat board slides downwind | L | C1, B5 | Slice |
| C3 | Multi-point board sampling on waves | The board tracks a 1 m swell at 15 m/s without tunnelling | M | A5, C1 | Slice |
| C4 | Send and pop mechanics | A well-timed send and release at 30 kn gives 12 to 20 m; mistimed by 0.4 s gives under 60% of that | L | B3, B5, C2 | Slice |
| C5 | Hangtime calibration | Simulated `8h / t^2` falls in 1.5 to 3.5 m/s² for jumps of 10 to 35 m | S | C4 | Slice |
| C6 | Megaloop | A loop started at the apex gives peak tension of 3 to 5 body weights with line elevation under 15°; the catch restores elevation above 60° before touchdown on jumps of 15 m or more | M | C4 | Slice |
| C7 | Landing and crash model | Good landings report 3 to 6 g; a hot landing is flagged when sink exceeds 6 m/s or kite elevation is under 45°; a crash returns the rider to rideable within 2 s | M | C1 | Slice |
| C8 | Jump telemetry record with CSV export | Height, airtime, peak tension, landing g and loop count are logged per jump; height matches ground truth within 0.1 m | S | C1 | Slice |
| C9 | Kickers in the level | Take-off from a kicker face gives a higher apex than flat water at equal speed | M | C3, C4 | Depth |

### D. Controls and assists

| ID | Task | Acceptance criterion | Size | Depends on | Milestone |
| --- | --- | --- | --- | --- | --- |
| D1 | Sim-cade input actions | Heading, edge, kite steer, analog sheet, load and pop, grabs and reset exist as input actions on gamepad | S | | Slice |
| D2 | Kite position hold (auto-park) | With the stick released the kite settles within 5° of its commanded position inside 1.5 s, in 15 to 35 kn | M | B3 | Slice |
| D3 | Stick-gesture kite loop with loop catch | A right-stick half-circle in the air triggers a loop whose radius scales with gesture speed; the kite recovers to 12 | M | B3, C6 | Slice |
| D4 | Auto-redirect landing assist | With the assist on, 9 of 10 scripted 15 m jumps land under 4 g | M | C7 | Slice |
| D5 | Upwind assist, reset upwind and session marker | Reset is rideable within 2 s; the marker restores position, tack and at least 80% of prior speed | S | C1 | Slice |
| D6 | Control presets and independent assist toggles | Arcade, sim-cade and simulation presets are selectable; each assist toggles on its own; settings persist | M | D2, D3, D4 | Depth |
| D7 | Keyboard and mouse scheme | Every gamepad action has a keyboard or mouse binding; the mouse sets a kite target position | S | D1 | Depth |

### E. Presentation and feedback

| ID | Task | Acceptance criterion | Size | Depends on | Milestone |
| --- | --- | --- | --- | --- | --- |
| E1 | Visible placeholder rider, board and kite, and a visible ocean | All four are visible in a game-mode run of the default map | M | | Slice |
| E2 | Four-line renderer | Lines join bar and kite with no visible endpoint lag at 25 m/s; they sag when slack | M | B5, E1 | Slice |
| E3 | Two-target big air camera | For jumps of 5 to 30 m the rider is on screen all the time and the kite at least 80% of airtime; the camera does not roll with the board | L | C4 | Slice |
| E4 | Landing anticipation camera and decal | From the apex the predicted landing point is within 3 m of actual touchdown | S | E3 | Depth |
| E5 | HUD: jump card, power gauge, improved wind-window arc | Height, airtime and landing g appear within 0.5 s of landing; the arc shows elevation and commanded position | M | C8 | Slice |
| E6 | Visible gusts | Gust fronts show as dark water bands at least 3 s before arrival, aligned with the gameplay gust cells in a debug view | M | A4 | Slice |
| E7 | Spray and wake effects | Spray volume follows speed and edge load; a foam wake trails the board | M | C2 | Depth |
| E8 | Wind, line and water audio bed | Three parameters (apparent wind, line tension, board speed) drive the mix and are visible in the audio debugger | S | B5 | Slice |
| E9 | Line tension rumble | Rumble amplitude tracks line load with correlation above 0.9 in a logged test | S | B5 | Slice |
| E10 | Rider poses with IK, and kite canopy flutter | Feet stay on the board and hands on the bar through a full jump; the canopy flutters when luffing | L | E1 | Depth |
| E11 | Water rendering check on Vulkan | `r.Water.SingleLayer.DepthPrepass` evaluated on 5.8.3 with before and after `stat gpu` recorded | S | E1 | Depth |

### F. Tricks, scoring and modes

| ID | Task | Acceptance criterion | Size | Depends on | Milestone |
| --- | --- | --- | --- | --- | --- |
| F1 | Rider rotation in the air | Back roll and front roll are controllable from the left stick; landing orientation affects landing quality | L | C1 | Slice |
| F2 | Grabs and board-off | A grab can be held in the air; a board-off fails the landing if the board is not replaced | M | F1 | Slice (one grab) |
| F3 | Trick recognition and naming | A megaloop back roll is labelled as such; loop count, loop direction, rotations and board-off compose into a name | M | F1, C6 | Depth |
| F4 | Per-jump score from height, extremity, technicality and execution | A 20 m loop beats a 20 m straight jump; a crashed landing scores 30% or less of a clean one; a low megaloop scores under a high megaloop | M | C8, F3 | Depth |
| F5 | Best-three session mode | A 90 s timer, the best three jumps summed, a results screen with a local best; a repeated trick does not raise the total | S | C8 | Slice (height only) |
| F6 | Failure cause message | After a failed jump a one-line cause is shown, such as popped late or kite too low | S | C4, C7 | Depth |

### G. Content and retention

| ID | Task | Acceptance criterion | Size | Depends on | Milestone |
| --- | --- | --- | --- | --- | --- |
| G1 | Spot presets | Three spots load distinct wind direction, gust profile and water state: kickers at 35 kn, a flat gusty lagoon, a steady flat lagoon | M | A4, C9 | Depth |
| G2 | Recorder for replay and ghosts | A ghost reproduces a 60 s run within 5 cm; the last 20 s replays on one button with kite and drone cameras | L | A2, E3 | Depth |
| G3 | Onboarding: beach kite drill and guided first jump | A tester with no kite knowledge hits six window targets in under 90 s and lands a 5 m jump, with no text beyond input glyphs | M | D2, D4 | Depth |
| G4 | Challenges, trick book and records board | Three challenges per spot; tricks unlock on first landing; twin-tip and foil records are kept apart | M | F3 | Retention |
| G5 | Heat and tour mode | Heat score equals best three tricks plus impression at 25% each; heats chain into a tour | M | F4 | Retention |
| G6 | Daily wind with leaderboards | One seeded wind per spot per day with a shared leaderboard | L | G2 | Retention |
| G7 | Accessibility suite and adaptive triggers | One-handed mode, remapping, colour-safe HUD, game-speed slider; trigger resistance follows bar pressure on a DualSense | L | D6 | Retention |

The vertical slice is every row marked Slice. Its exit test is that a non-kiter lands a jump above 10 m within 10 minutes and chooses to play a second session.

## Open questions and sources

### Decisions needed before tasks are created

- [ ] **Default realism level.** The backlog assumes sim-cade controls with assists on by default. Confirm, or choose simulation-first.
- [ ] **Slice conditions.** Proposed: 9 m² kite, 22 m lines, 30 kn with gusts, one flat-water spot. Kickers come in the Depth milestone.
- [ ] **Art source.** Placeholder meshes for rider, board and kite, or bought assets.
- [ ] **Committed token.** The Android file-server `SecurityToken` is in `Config/DefaultEngine.ini`. Decide whether it should be removed from the repository.
- [ ] **Rider rotation in the slice.** Task F1 is large. Dropping it from the slice leaves jump, kiteloop and grab only.

### Not verified

- **Line tension.** No measured tension data for kite jumps or megaloops was found. All take-off, loop and landing-speed figures are derived estimates.
- **Lift and drag table, and the Savitsky planing formula.** Both were recalled from memory, not read from a source.
- **Steering gains and dead times.** Read through a summarising fetch; re-check against the paper's table.
- **King of the Air format.** Red Bull's own format pages would not load. Scoring comes from brand explainers; round structure comes from search summaries.
- **Record progression before 2025** and five of the seven spot rows come from search summaries.
- **Trick difficulty ratings** and prerequisites are editorial estimates.
- **Vulkan issues on 5.8.3.** Whether the Single Layer Water artifact and the device-loss crashes still occur on this engine version was not tested.
- **Engine APIs.** Class and cvar names were checked in the 5.8.3 source, but their behaviour was not exercised. Niagara tick settings, MetaSound node names and Low-Level Tests setup were not checked.
- **Code audit inferences.** Endless climb above 19 kn, the sinking landing, the one-frame force delay and camera roll were reasoned from code, not observed.
- **All game-design tuning numbers** (FOV, timings, thresholds, multipliers) are starting proposals.

### Sources

Physics:

- [Fechner et al., dynamic model of a pumping kite power system](https://arxiv.org/pdf/1406.6218)
- [Oehler and Schmehl 2019, aerodynamic characterisation of a soft kite](https://wes.copernicus.org/articles/4/1/2019/)
- [Wind Energy Science 2024, kite turn-rate measurements](https://wes.copernicus.org/articles/9/2261/2024/)
- [Loyd 1980, crosswind kite power](https://awesco.eu/awe-explained/Loyd1980.pdf)
- [WOO sensor accuracy study](https://pmc.ncbi.nlm.nih.gov/articles/PMC8706814/)
- [Cabrinha Switchblade specifications](https://www.cabrinha.com/products/switchblade-2)

Sport:

- [GKA big air discipline](https://www.gkakiteworldtour.com/discipline-big-air/), [2025 rulebook](https://www.gkakiteworldtour.com/rulebooks/2025/GKA%20RULEBOOK%202025.pdf) and [trick list](https://www.gkakiteworldtour.com/wp-content/uploads/2023/04/Tricklist-Big-Air-TT-2023.pdf)
- [North: judging criteria explained](https://northactionsports.com/blogs/all/judging-criteria-explained) and [big air tricks explained](https://northactionsports.com/blogs/all/big-air-tricks-explained)
- [Mystic: King of the Air scoring](https://www.mysticboarding.com/blogs/news/how-to-qualify-for-red-bull-king-of-the-air)
- [Red Bull: King of the Air wind conditions](https://www.redbull.com/ca-en/red-bull-king-of-the-air-wind-conditions-explained)
- [Jamie Overbeek's 42.3 m record](https://bb-talkin.eu/blogs/news/jamie-overbeek-bbtalkin-42-3m-record-story) and its [landing analysis](https://www.mackiteboarding.com/news/jamie-overbeeks-423m-jump-descent-and-landing-analysis/)
- [Hugo Wigglesworth's 40 m jump](https://www.iksurfmag.com/kitesurfing-news/2025/12/hugo-wigglesworths-40-meter-kite-jump-a-new-world-record/)
- [Line lengths and kiteloops](https://bigairkite.com/blogs/whats-the-deal/what-s-the-deal-line-lengths-kiteloops)
- [WOO big air metrics](https://docs.woosports.com/docs/kite-big-air)

Game design:

- [Kiteboarding Pro](https://store.steampowered.com/app/1629300/Kiteboarding_Pro/), [Winds Up Kitesurfing](https://store.steampowered.com/app/2799240/Winds_Up_Kitesurfing/) and [Kitesurf Runner](https://store.steampowered.com/app/3025260/Kitesurf_Runner/) on Steam
- [Kiteboard Hero review](https://kitesista.com/kiteboard-hero-kiteboarding-game-actually-works/)
- [Lonely Mountains: Downhill design interview](https://www.gamedeveloper.com/business/road-to-the-igf-megagon-industries-i-lonely-mountains-downhill-i-)
- [Session review](https://www.shacknews.com/article/132464/session-skate-sim-review)
- [Skate control presets](https://www.ea.com/games/skate/skate/news/get-control)

Unreal Engine:

- [Single Layer Water issues on Vulkan](https://forums.unrealengine.com/t/vulkan-and-dx12-parity-singlelayerwater-issues/2720975)
- [Vulkan device loss on Linux, NVIDIA](https://forums.unrealengine.com/t/ue-5-8-release-instant-vulkan-crash-vk-error-device-lost-on-linux-with-rtx-3090-ti-nvidia-driver/2729632) and [AMD](https://forums.unrealengine.com/t/ue-5-8-vulkan-on-linux-triggers-vk-error-device-lost-gpuvm-read-fault-on-amd-rx-9070-xt-with-radv/2730749)
- [Async physics tick and sub-stepping](https://forums.unrealengine.com/t/relationship-between-tick-physics-async-and-substepping/2395435)
- [Wave height query behaviour](https://forums.unrealengine.com/t/watersubsystem-how-does-getwaveheightatposition-work/2148134)
- [Unreal Engine 5.8 performance highlights](https://tomlooman.com/unreal-engine-5-8-performance-highlights/)
- Installed engine source, version 5.8.3, under `/opt/unreal-engine`
