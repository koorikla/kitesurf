# Physics research

Phase 2 of the physics rework. What the real physics says, what established game approaches
do, and what this game should take from each. Read with `review.md` (what the code does today)
and `plan.md` (what changes). The project's earlier research, `docs/research.md`, covers the
sport, the competition formats and the game design; this document only covers the physics and
the simulation and goes deeper there.

Every number carries a tag: **sourced** (read in the cited source during this phase),
**typical** (textbook or industry value, not re-read), **estimate** (derived here from a
formula or a force balance). Estimates are tuning starting points, not requirements.

## 1. Kite aerodynamics

### 1.1 Apparent wind at the kite

The air the kite flies in is the true wind at the kite's height minus the kite's own world
velocity:

```
v_a = v_wind(p_kite, t) - v_kite
v_kite = v_rider + v_kite,relative            (the kite rides on the rider's lines)
```

Three consequences, all of which the game must keep:

- A parked kite feels `v_wind - v_rider`: riding across the wind at 10 m/s in 12 m/s of wind
  gives it a 15.6 m/s airspeed, 1.3 times the true wind, from a direction 40 deg forward of the
  true wind. That is why the wind window the rider feels rotates towards the bow as they
  accelerate, and why a fast rider cannot park the kite as far forward as a slow one.
- A kite moving across the window adds its own speed. In steady crosswind flight a kite would
  reach `v_wind * sqrt(1 + (L/D)^2)`, 4 to 5 times the wind for L/D of 4 to 5
  (Loyd 1980, **sourced** in `docs/research.md`). On 22 m lines it never gets there; realistic
  peaks are 2 to 3 times the wind, 25 to 35 m/s in 30 kn (**estimate**).
- Force scales with airspeed squared, so a kite diving through the power zone at twice the
  wind speed pulls four times as hard as the same kite parked at the edge. The jump and the
  loop are both built on this.

The wind at the kite's height is also stronger than at the rider (section 4.1): about 10%
more at 24 m than at 10 m, and the rider at 1.5 m sees about 20% less than at 10 m.

### 1.2 Lift and drag against angle of attack, with stall

Measured on a leading-edge-inflatable (LEI) tube kite in flight (Oehler and Schmehl 2019,
**sourced**): lift-to-drag ratio about 4 powered and about 3 depowered, with a peak of about 5
for angles of attack of 5 to 10 deg; angle of attack 6 to 16 deg in the powered phase and
-7 to +3 deg depowered. Fechner et al. (2015, **sourced**) model the same kind of kite with
lift and drag lookup tables against angle of attack and a steering-induced drag factor
`C_D * (1 + 0.6 |u_s|)` with `K_s,D = 0.6`.

A curve that reproduces those numbers and is cheap to evaluate (**estimate**, assembled from
thin-aerofoil and finite-wing theory with the measured ratios as targets):

```
alpha_e   = alpha - alpha_0                          alpha_0 ~ -3 deg (camber: lift at zero geometric angle)
C_L       = C_L,a * alpha_e                          attached, alpha < alpha_stall
C_L,a     = 2 pi AR / (AR + 2)  ~ 4.3 /rad  (0.075 /deg) for projected aspect ratio AR ~ 4
C_L,max   = 1.0 to 1.2 at alpha_stall ~ 16 to 20 deg
C_D       = C_D0 + C_L^2 / (pi AR e) + C_D,lines      C_D0 ~ 0.08 to 0.10, e ~ 0.8, C_D,lines ~ 0.01
```

Check: at alpha = 10 deg, `C_L = 0.075 * 13 = 0.98`, `C_D = 0.09 + 0.98^2 / (pi * 4 * 0.8) + 0.01
= 0.195`, L/D = 5.0. At alpha = 2 deg, `C_L = 0.37`, `C_D = 0.11`, L/D = 3.3. Those are the
measured peak and depowered values.

Past the stall the canopy is a plate: normal force `C_N ~ 1.2 sin(alpha)` for a three-dimensional
plate at high angles (Hoerner, **typical**; the code's 1.8 is at the high end of what flat plates
show), resolved into lift `C_N cos(alpha)` and drag `C_N sin(alpha)`, blended in over 4 to 6
deg so the stall is a progressive loss, not a switch. Two stall modes matter for play:

- **Backstall** (the "hindenburg"): angle of attack above the stall at low airspeed, typically
  from sheeting in while the kite is slow or deep in the window. Lift plateaus, drag jumps, the
  kite falls back tail-first.
- **Luff**: angle of attack below about -5 deg, when the kite overflies the window edge or a
  depowered kite is flown fast. The canopy unloads and the lines go slack (**typical**).

Reynolds number effects and canopy deformation are ignored; a tube kite's chord Reynolds number
is a few million and the curve above is already a fit.

### 1.3 The wind window: power zone, edge, zenith

Lines of length L put the kite on a sphere. In azimuth `a` (from downwind) and elevation `b`:

```
p_kite = p_rider + L * [cos(b) cos(a), cos(b) sin(a), sin(b)]
```

- **Parked at the edge.** With a massless kite the aerodynamic resultant must point along the
  line, so the line sits at `atan(L/D)` from the downwind axis: 76 deg for L/D 4, 79 deg for 5.
  The window edge is not at 90 deg; the last 10 to 15 deg are where the kite cannot fly
  (**estimate**, from the force direction; this is the check the code already passes at about
  10 deg inside the edge). Gravity pulls the resting position lower and, near the horizon,
  further into the window.
- **Zenith.** Overhead the apparent wind is horizontal and perpendicular to the lines, so the
  lift is along the lines and the drag pushes the kite a little downwind of the zenith until the
  line's tilt balances it: `atan(D/L)` = 11 to 14 deg. Tension there is about
  `0.5 rho v_a^2 A_proj C_L - m g`. For a 9 m^2 kite (projected area about 0.72 x 9 = 6.5 m^2,
  **typical**) at 30 kn (15.4 m/s) sheeted in (`C_L` 1.0): 940 N, about 1.1 body weights for an
  85 kg rider (**estimate**). Depowered (`C_L` 0.4): 380 N.
- **Power zone.** The same kite flown down through the centre of the window reaches 2 to 3
  times the wind speed, and tension 4 to 9 times the parked-at-edge value (**estimate**). The
  steady-state crosswind limit, 17 to 26 times, is never reached on surf lines.
- **Kite speed in the window** comes from the force balance, not from a speed law: the component
  of the aerodynamic force along the sphere accelerates the kite, the line removes the radial
  part. A point mass on a sphere with lift, drag, weight and the line constraint reproduces all
  of the above without any special cases, which is why the current model's structure is right.

### 1.4 Steering and loops

The measured law for tethered membrane wings (Fechner et al. 2015, eq. 72; validated again
with automated manoeuvres by Elfert, Goehlich and Schmehl 2024; both **sourced**):

```
heading_rate = g_k * v_a * delta(t - t_dead)  +  (c_2 / v_a) * sin(heading) * cos(elevation)
```

- `g_k` (also `c_1`) is the steering gain: 0.261 rad/m for the 10 m^2 Hydra kite (Fechner,
  fitted); 0.15 rad/m at minimum power to 0.35 rad/m at full power for a 10 m^2 Vegas kite
  (Elfert 2024). So **depower roughly halves the turn rate**.
- `t_dead` is the dead time between bar input and the kite responding: about 200 ms at full
  power rising to about 600 ms at minimum power (Elfert 2024). A fully depowered kite is slow
  to answer the bar as well as slow to turn.
- `c_2` is the gravity term: 6.28 rad m/s^2 for the Hydra (Fechner, fitted). It turns the nose
  down when the kite is slow and flying across, which is what makes a stalled or slow kite fall
  nose-first and what lets a kite "sit" at the edge with the nose slightly up.
- The turn radius is `1 / (g_k * delta)`, independent of airspeed: 2.9 m at full bar powered,
  6.7 m depowered for the 10 m^2 kite. Loop time is `2 pi R / v_kite`: a 9 m^2 kite at
  `g_k` 0.25 rad/m (R = 4 m) moving at 15 m/s loops in 1.7 s, in the 1.5 to 2.5 s range riders
  report (**estimate**).
- Smaller kites turn faster. Measured gains scale roughly with `1 / sqrt(area)` (**estimate**;
  the earlier research quotes 0.21 to 0.35 rad/m for 6 m^2 and 0.09 to 0.19 for 13 to 14 m^2,
  not re-read here).
- Steering costs drag: `C_D * (1 + 0.6 |delta|)` (Fechner, **sourced**). A kite held hard over
  in a loop is draggy, which caps loop speed.

### 1.5 Depower: what the bar and the trim do

Sheeting the bar in shortens the rear (steering) lines relative to the front lines and so
pitches the canopy to a higher angle of attack; sheeting out does the opposite. The trim strap
(or "depower strap") shifts the whole range. Measured angle-of-attack ranges give the throw:
6 to 16 deg powered against -7 to +3 deg depowered (Oehler and Schmehl, **sourced**), so the bar
is worth 12 to 20 deg of angle of attack (**estimate**; the earlier research says 12 to 20
and the code uses 24).

Depower therefore changes three things at once, and a model that only changes lift is missing
two of them: lift coefficient (power), turn rate (halved, section 1.4) and dead time (tripled).

### 1.6 Lines: tension, count, stretch, drag

- **Four lines.** Two front lines run from the bar's centre lines to the leading edge and carry
  the power; two back lines run from the bar ends to the wingtips and set angle of attack and
  steering. Sheeted out the front lines carry 70 to 90% of the load; sheeted in the back lines
  take more (**estimate**). A **fifth line** on some C-kites runs to the centre of the leading
  edge for relaunch and safety and takes load when the kite is depowered through it. For a
  point-mass model the line count only matters for the visuals and for where the load goes; the
  tension that matters to the rider is the resultant along the bridle.
- **Length.** 20 to 24 m standard, 17 to 18 m for loops (**sourced** in `docs/research.md`).
  Shorter lines give faster angular travel, tighter loops and an earlier catch.
- **Stretch.** Fechner's 4 mm Dyneema tether has a unit spring constant `k_0` = 614,600 N, so
  `k = k_0 / L` (**sourced**). Scaling with cross-section to a 1.5 mm kite line gives
  `k_0` of about 86 kN, 3.9 kN/m per 22 m line, 8 kN/m for the two front lines in parallel and
  about 12 kN/m for all four (**estimate**). At 2.5 kN that is 0.2 to 0.3 m, or about 1%, in
  line with the 1 to 1.5% elongation line makers quote (**typical**). The stored energy at 2.5 kN
  is about 300 J, 0.4 m of jump height: small, but the stretch is what makes a hard load feel
  springy rather than like a wall.
- **Drag.** Four 22 m lines of 1.5 mm are 0.13 m^2 of cylinder at `C_D` about 1. The airspeed
  along a line varies from the rider's to the kite's, which integrates to about a third of the
  kite's airspeed squared, so the lines add about 0.007 to the kite's drag coefficient on a
  6.5 m^2 projected area (**estimate**; the earlier research's 0.01). Worth one constant, not a
  model.
- **Mass.** About 0.1 kg for four lines. Ignore.

### 1.7 Kite size and mass

- Mass against flat area, from the Cabrinha Switchblade (**sourced** in `docs/research.md`):
  7 m^2 2.65 kg, 9 m^2 3.11 kg, 12 m^2 3.79 kg, 14 m^2 4.17 kg. A line fit is
  `m ~ 1.1 kg + 0.22 kg/m^2 * A` (**estimate**).
- Projected area is about 0.65 to 0.80 of flat area (**typical**); force scales with projected
  area and airspeed squared.
- Wind range for a 75 kg rider (**sourced**, same page): 7 m^2 18 to 32 kn, 9 m^2 13 to 27 kn,
  12 m^2 9 to 21 kn. The rule of thumb `area = 2.2 * kg / knots` the code uses sits inside those
  ranges.
- Turn rate falls with size (section 1.4) and the turn radius grows roughly with the span.
- Added mass: a canopy accelerating through air carries some air with it. For a thin lifting
  surface the added mass is of the order of the mass of air in a cylinder of the chord's
  diameter and the span's length, `rho * pi (c/2)^2 * b`, about 2 to 4 kg for a 9 to 12 m^2 kite
  (**estimate**). Comparable to the kite's own mass, so it belongs in the inertia.

## 2. Board hydrodynamics

### 2.1 Displacement, hump and planing

A hull planes once dynamic lift carries most of its weight, which for a short hull happens
around a length Froude number `v / sqrt(g L)` of 1.0 to 1.2 (**typical**). For a 1.38 m
twin-tip that is 3.7 to 4.4 m/s, 7 to 9 kn; the code's 4 m/s threshold is in range. Below it the
board sits in the water and drag rises steeply with speed (the "hump"); above it drag falls as
the wetted area shrinks, then rises again as spray and friction grow. A twin-tip under a kite
is pulled onto the plane by the kite rather than driven over the hump, which is why the water
start is about getting the kite to pull before the board has any lift.

### 2.2 Planing lift and drag against speed

Savitsky's method (1964, **sourced** as the standard reference; the formulas below are the
method as published, **typical**) for a prismatic planing surface of beam `b`, trim `tau` (deg),
mean wetted length ratio `lambda = l_m / b` and speed coefficient `C_V = v / sqrt(g b)`:

```
C_L0   = tau^1.1 * (0.0120 lambda^0.5 + 0.0055 lambda^2.5 / C_V^2)      lift coefficient, flat plate
C_L,b  = C_L0 - 0.0065 beta C_L0^0.6                                     deadrise correction, beta in deg
Lift   = 0.5 rho v^2 b^2 C_L
Drag   = W tan(tau) + 0.5 rho v^2 lambda b^2 C_f / cos(tau)             pressure drag plus friction
```

Worked for a twin-tip (`b` 0.41 m, `beta` ~ 0, carrying 900 N at 10 m/s, **estimate**):
`C_V` = 5.0; the lift needed gives `C_L` 0.107; at `tau` 6 deg that needs `lambda` about 1.2,
so the wetted length is about 0.5 m of the 1.38 m board. Drag: 95 N of pressure drag plus about
40 N of friction (`C_f` 0.004) = 135 N, L/D about 6.7. Over 8 to 15 m/s the pressure drag
falls (trim and wetted length shrink) while friction grows, so total drag is roughly flat to
gently rising, 100 to 200 N (**estimate**). Edged, the rail is also making side force and the
effective L/D drops to 3 to 5 (**estimate**).

For a game, `Drag = a + c v^2` on the plane with `a` ~ 60 N and `c` ~ 0.5 to 1.0 N s^2/m^2
reproduces that band, and the load the board carries is the rider's weight minus the vertical
line pull, which falls as the kite climbs.

### 2.3 Fins, rail and edging: lateral force and upwind ability

The board makes side force the way a wing does: from leeway angle (the angle between the
board's axis and its velocity through the water) and from heel. The lifting surfaces are the
fins (four of 4 to 5 cm, about 0.01 m^2 each, **typical**) and the immersed rail (wetted length
times immersed depth, about 0.5 m x 0.05 m at a 40 deg heel, **estimate**):

```
F_side = 0.5 rho_w v^2 A_lat C_L,beta * beta         C_L,beta ~ 2 to 3 /rad for these low aspect ratios
```

At 10 m/s with `A_lat` 0.065 m^2 that is about 8 kN per radian of leeway, so holding 400 N of
sideways pull needs about 3 deg of leeway; 1 kN needs 7 deg (**estimate**). Riders do slip a
few degrees. Heel does two things: it puts more rail in the water (more `A_lat`), and it tilts
the board's normal force sideways so the water carries the horizontal pull directly:

```
N sin(phi) = T_horizontal            N cos(phi) = m g - T_vertical
tan(phi)   = T_horizontal / (m g - T_vertical)
```

For 0.7 body weights of tension at 35 deg elevation the heel is about 44 deg (**estimate**,
from `docs/research.md`). Fins do 10 to 20% of the work at high heel (**estimate**); at low
heel on a flat board they do most of it, which is what holds a course with no edge.

**Upwind limit.** The closest course to the apparent wind is set by the drag angles of the two
foils (the course theorem of sailing theory, Marchaj, **typical**):
`beta_course = atan(1 / (L/D)_kite) + atan(1 / (L/D)_board)`. With both at 4 that is 28 deg from
the apparent wind. Because the apparent wind is itself forward of the true wind, that works out
as a true-wind course 15 to 25 deg above a beam reach (**typical**), and 2 to 3 m/s of velocity
made good upwind at riding speed (**estimate**). Edging harder past the optimum does not point
higher: it adds drag and slows the board, and a slower board sees the apparent wind go aft.

### 2.4 Twin-tip against directional

A twin-tip is symmetric fore and aft (rocker 3 to 5 cm, 132 to 145 cm long, 40 to 42 cm wide,
2.5 to 3.5 kg, **typical**): it rides either way, lands either way, and swaps ends under the
rider on a transition. A directional (surfboard) has a nose, more volume, a fin cluster at one
end, needs a gybe or a jump to turn round, and planes earlier. The game models the twin-tip
only; the differences that would matter for a directional are the asymmetric lift and the
turn-round, not the physics core.

## 3. Rider

### 3.1 Force balance on the water

Three forces hold a rider up and across: line tension `T` through the harness hook, the
water's force on the board `N` (normal to the planing surface, tilted by heel), and weight.
Horizontally `T_h` is balanced by `N sin(phi)` plus the board's drag along the course;
vertically `m g = T_v + N cos(phi)`. The rider's body lines up with the resultant of weight and
tension, so the lean from vertical equals the heel in the balance above: 30 to 60 deg while
riding (**typical**). The hook sits about 1.0 m above the feet and 0.1 to 0.15 m in front of the
centre of mass (**estimate**), which is why the body hangs slightly bow-forward under load and
why a rider with their back to the kite gets pulled round.

For the simulation the point mass takes the tension at the hook height; the lean is read off
the force ratio and drawn, not integrated. Riding tension is 0.5 to 1.0 body weights
(**estimate**, `docs/research.md`).

### 3.2 Sheeting

Bar throw is 0.4 to 0.55 m (**typical**). Pulling the bar in loads the rider's arms with the
back-line share of the tension, which is why real riders cannot hold a fully sheeted kite in a
gust and let the bar out instead. Sheeting is the throttle: it sets angle of attack (power),
turn rate and dead time (section 1.5), and since the kite then flies to a new balance it also
moves the kite in the window: sheeted in it sits deeper, sheeted out it drifts to the edge.

### 3.3 Jumps: load-and-pop against kite lift, and hang time

Two sources of height:

- **Pop**: leg extension gives 1 to 2 m/s vertical, and the board's flex and the rail unloading
  give a little more (**estimate**). A pop alone is under 1 m.
- **The send**: the kite is steered back through 12 while the rider holds an edge, so tension
  builds (2.5 to 4 body weights at take-off, **estimate**) and rotates towards vertical. The pop
  is the moment the edge lets that vertical force act. The kite keeps doing work through the
  climb: 85 kg to 15 m is 12.5 kJ, far more than the rider's kinetic energy (5 kJ at 11 m/s).

Hang time is the number that tells whether the kite is carrying the rider on the way down. With
effective gravity `g_eff = 8 h / t^2`, real jumps give 1.5 to 3.5 m/s^2 (**sourced** in
`docs/research.md`, from WOO height and airtime pairs), so the kite carries 65 to 85% of the
rider's weight averaged over the flight: a 15 m jump lasts 5 to 7 s, a 10 m jump 4 to 5 s. The
requirements on the model are therefore: a kite that can be held overhead and sheeted in while
the rider is airborne, pulling about 1 body weight along lines that are nearly vertical, and a
rider whose descent speed stays at 3 to 6 m/s rather than free fall. Where the kite is low or
depowered on the way down, the rider drops at 8 to 12 m/s and the landing is "hot".

Loops in the air pull 3 to 5 body weights, mostly sideways and down (**estimate**); the catch
is the kite climbing back to the zenith on its remaining speed.

### 3.4 Landing

Measured landing loads are 4.2 to 5.5 g on 1.6 to 5.6 m jumps with peak foot force 31.7 N/kg
(Simons 2025, **sourced** in `docs/research.md`). A model that matches: the sink speed is taken
out over the absorb distance `s` of the legs and the board's immersion, 0.2 to 0.4 m, so
`g_landing ~ 1 + v_sink^2 / (2 g s)`: 4 m/s into 0.3 m is 3.7 g, 7 m/s is 9 g (**estimate**).
That gives a number with meaning for the HUD and a threshold for crashes, and it costs one
line.

### 3.5 Body drag

A rider is about 0.5 to 1.0 m^2 of drag area (**estimate**): 40 N at 10 m/s of apparent wind,
270 N at 25 m/s in a loop. Negligible while riding against the water forces; noticeable in the
air, where it is the only thing slowing the rider besides the lines. Worth one term in the
airborne state.

## 4. Wind

### 4.1 The boundary layer: more wind aloft

Over water under near-neutral stability the mean profile follows a power law with exponent
0.11 +- 0.03 (Hsu, Meindl and Gilhousen 1994, **sourced**), or a log law
`u(z) = (u_* / 0.4) ln(z / z_0)` with sea roughness `z_0` of 0.1 to 0.5 mm in 10 to 30 kn
(Charnock, **typical**). The two agree to a few percent between 1 and 30 m. With the 10 m wind
as the reference (`alpha` = 0.11):

| Height | 1 m | 1.5 m | 2 m | 10 m | 15 m | 20 m | 25 m |
| --- | --- | --- | --- | --- | --- | --- | --- |
| u / u_10 | 0.78 | 0.81 | 0.84 | 1.00 | 1.05 | 1.08 | 1.11 |

So a kite at 20 to 25 m on 22 m lines sees 8 to 11% more wind than the 10 m figure, and 30 to
40% more than the rider's chest at 1.5 m. Tension scales with the square, so the kite aloft
pulls about 20% harder than a 10 m wind would suggest. The HUD and the gear screen should keep
quoting the 10 m value, which is what forecasts and anemometers report; the rider's apparent
wind and the window axis should use the wind at about 1.5 m.

The code's log-shaped ramp that stops at 10 m under-reads the kite by about 10% and over-reads
the rider at the water by using `Z = 0`, where a log law is undefined.

### 4.2 Turbulence intensity and gust factor

Turbulence intensity `sigma_u / U` at 10 m over open water is 8 to 12% in moderate winds
(**typical**; the IEC 61400-1 normal turbulence model gives `sigma_u = I_ref (0.75 U + 5.6)`
with `I_ref` 0.12 for its lowest class, 13% at 15 m/s, and open sea sits below that). The
cross-wind and vertical components are about 0.8 and 0.5 of the along-wind one (**typical**,
IEC), so the direction wanders with a standard deviation of about 0.8 x TI radians: 4 to 6
deg.

The gust factor, the ratio of the 3 s peak to the 10 min mean, is 1.23 at sea and 1.38 for
wind arriving off the sea at a coast (WMO/TD-1555, Harper, Kepert and Ginger 2010, **sourced**).
In a 25 kn mean the strongest 3 s gusts in ten minutes are therefore 31 to 35 kn, and lulls go
about as far the other way. Spots with gusts of 10 to 15 kn over the average
(`docs/research.md`) are the upper end of this.

### 4.3 A gust and lull model for real time

The energy of the along-wind fluctuations is spread over frequencies by the Kaimal spectrum
(Kaimal et al. 1972; the IEC form, **typical**):

```
f S_u(f) / sigma_u^2 = 4 (f L / U) / (1 + 6 f L / U)^(5/3)
L = 8.1 * 0.7 z   for z < 60 m      (57 m at 10 m, 85 m at 15 m)
```

The spectrum peaks at periods of about `4 L / U`, 20 s at 12 m/s and 10 m, and falls as
`f^(-5/3)` above that: most gust energy is in periods of 5 to 60 s, with rise times of 1 to 5 s
and a long tail of slow swells. The von Karman spectrum is the same shape with slightly
different constants.

Options for a game, cheapest first:

1. **Octaved gradient noise in time** (what the code does): three octaves with amplitudes
   halving per octave have a power slope of about `f^(-2)`, close enough to `-5/3` that nobody
   can tell. Deterministic for a seed, cheap, and it extends to space for free. Its weakness is
   the amplitude distribution (Perlin rarely reaches its bounds), so the gust factor must be set
   from the measured output, not from the nominal amplitude.
2. **Shaped filtered noise**: white noise through a first-order filter with time constant
   `L / U` gives a `-2` slope and a Gaussian amplitude; two in series give a sharper roll-off.
   Per-component (u, v) with the 0.8 ratio gives direction wander. Needs state per sample point,
   so it suits a single global time series, not a field.
3. **Spectral representation**: a sum of 8 to 16 sinusoids with random phases, amplitudes from
   the spectrum. Exact spectrum, deterministic, periodic over the longest period chosen.

Option 1 extended to a travelling field (section 4.4) is the right fit here: one evaluation
gives wind at any point and time, it is deterministic for a seed, and it serves gameplay,
water darkening, spray and audio alike.

### 4.4 Gust fronts that cross the water

Turbulent eddies are carried along at the mean wind speed and change slowly as they go
(Taylor's frozen-turbulence hypothesis, Taylor 1938, **typical**): what a fixed anemometer sees
as a time series is a spatial pattern blowing past. The gust that reaches the rider in 3 s is
now 3 x U upwind, 35 m at 12 m/s, and it is already darkening the water there. So:

```
u(p, t) = U(z) * (1 + sigma_rel * n(p - U_mean * t, t / T_slow))
```

with `n` a two-octave noise over the horizontal plane scrolled downwind at the mean speed, and
a slow extra time axis (`T_slow` of a minute or more) so patterns evolve rather than repeat.
Cell sizes along the wind of 50 to 100 m (the integral length scale `L`) and about half that
across the wind give gusts that last `L / U`, 5 to 10 s, and are a few boat-lengths wide. A
third, lower-amplitude noise on the direction gives the shifts. The same field sampled at the
water surface drives the dark bands that let the player time a send.

Squalls and fronts (a step change that lasts minutes) are a different, rarer thing and are
left out.

### 4.5 Determinism and a single field

The wind must be a pure function of position, time and a seed, owned by the world rather than
by a pawn: `GetWindAt(WorldPos, TimeSeconds)`. Then the kite, the rider, the HUD, the water
material and the audio all agree, a ride can be replayed from its seed and inputs, and tests can
pin time. Gradient noise with a hashed seed gives this directly.

## 5. Simulation

### 5.1 Fixed step with an accumulator and render interpolation

The standard pattern (Fiedler, "Fix Your Timestep!", **typical**): accumulate frame time,
step the simulation at a fixed `dt` while the accumulator holds at least `dt`, cap the number
of steps per frame so a hitch cannot run away (and clamp the frame time, 0.1 s is usual), keep
the previous and current states, and render at the interpolated state
`lerp(prev, curr, accumulator / dt)`. Results are then identical at any frame rate above the
step cap, and the one-frame latency between kite and board disappears because both step
inside the same loop in a fixed order.

Step size: 1/240 s is more than enough for a 3 kg kite under 1 to 6 kN forces (velocity change
per step under 1 m/s) and for the board (section 5.3); 1/120 s would do if cost mattered,
which it does not: the whole step is a few hundred floating-point operations.

### 5.2 Stiff tethers: constraints, not springs

A Dyneema line at its real stiffness is a spring of about 8 kN/m on a 3 kg kite: a natural
frequency of 8 Hz, `omega dt` of 0.2 at 240 Hz. Semi-implicit Euler is stable there, but only
just, and the rider's end couples an 85 kg mass through the same spring; any extra stiffness
(a shorter line, a heavier load) or a bigger step and it rings or explodes. The usual game
answer:

- **Distance constraint with projection** (Position Based Dynamics, Mueller et al. 2007,
  **typical**): after the free step, project the kite back to line length and remove the radial
  relative velocity. This is what the code does now. It is unconditionally stable and the
  tension needed for the projection is the line force. Its one flaw is that it is infinitely
  stiff.
- **XPBD** (Macklin, Mueller and Chentanez 2016, **typical**) adds a compliance
  `alpha = 1 / k` to the same constraint: `d_lambda = (-C - alpha_tilde lambda) / (w_1 + w_2 +
  alpha_tilde)` with `alpha_tilde = alpha / dt^2`, which gives exactly the spring's stretch
  without the spring's stability limit, and the accumulated `lambda` is the tension in newtons.
  A damping term handles the line's internal damping. One iteration per sub-step is enough when
  the step is small; sub-steps beat iterations (Macklin et al. 2019, "Small steps", **typical**).
- **Unilateral**: the constraint only acts when the distance exceeds the length and the
  multiplier would be positive. Slack is then free motion, and the snatch when the line comes
  tight is handled by the projection removing the outward velocity.

So the current approach is kept, made compliant with the real stiffness, and run at the fixed
step. The kite's own inertia should include the added mass (section 1.7).

### 5.3 Drag terms and damping without frame-rate dependence

Explicit integration of a linear drag `F = -c v` is stable only for `dt < 2 m / c` and
accurate only well below that. The board's lateral grip is such a term with `c / m` up to
73 /s, which is why the code caps it by `m / dt`. The fix is to integrate drag implicitly or
exactly within the step:

```
linear:     v_new = v / (1 + (c / m) dt)             or exactly  v * exp(-(c / m) dt)
quadratic:  v_new = v / (1 + (k / m) |v| dt)
```

Both are unconditionally stable, never overshoot, and give the same decay at any step. The
buoyancy spring is better written as a damped spring with its stiffness and damping ratio named
(`omega_n`, `zeta`) and integrated semi-implicitly, so `zeta` can be read as "0.7 feels like
water" rather than as 800 of something.

### 5.4 Determinism and cost

With a fixed step, a fixed update order and a seeded wind, the same inputs reproduce the same
ride on the same machine. Across machines floating-point results can differ in the last bits
(fused multiply-add, compiler flags), so shared ghosts should carry recorded transforms, not
inputs. Cost: the kite and board steps together are well under 1 microsecond each; four
sub-steps per frame plus wind sampling is noise next to one draw call. Water queries against
the Water plugin are the only cost worth watching: five samples per step at 240 Hz is 1200
queries per second, fine, but keep them per step, not per sub-step, if the plugin's query
turns out slow.

## 6. What the game takes, and what it simplifies

| Topic | Taken from the physics | Simplified, and why |
| --- | --- | --- |
| Kite as a point mass on the line sphere with apparent wind | Yes, kept | No canopy deformation, no span-wise loading: the measured L/D and turn-rate laws already lump these |
| Lift and drag curve with camber, stall, luff | Curve of 1.2, targets from Oehler and Schmehl | One curve for all kites; models differ by a few scale factors (existing trait table) |
| Turn-rate law with gain, dead time, gravity term, depower coupling | All four terms, with Fechner's and Elfert's values scaled by size | Dead time as a short input delay line, not a transfer function |
| Lines | Unilateral compliant constraint at the real stiffness; drag as one coefficient | Four lines drawn, one resultant simulated; no line sag physics |
| Wind profile | Power law 0.11, 10 m reference | No stability classes, no sea-state coupling |
| Gusts | Octaved seeded noise advected at the mean speed, amplitude set from the gust factor | No real spectrum fit; no squalls |
| Board planing | `a + c v^2` drag calibrated to the Savitsky worked case; load carried = weight minus vertical pull | No trim and wetted-length solution per step |
| Board lateral force | Side force from leeway and heel with an explicit edge angle; fins plus rail | One lumped lateral lifting area, not four fins |
| Rider | Point mass at the hook; lean and heel from the force balance, drawn not integrated; body drag in the air | No rider rotation, no limb dynamics |
| Jump | Edge release lets the built-up vertical tension act; kite work during the climb; no scripted impulse beyond a small leg pop | Pop is an impulse, not a leg model |
| Landing | g from sink speed and absorb distance; crash by angle and g | No board flex, no water entry simulation |
| Integration | Fixed 240 Hz sub-steps in one accumulator, fixed order, render interpolation | Rider and board remain one point mass |

## 7. Sources

Read during this phase:

- Elfert, C., Goehlich, D. and Schmehl, R. (2024). Measurement of the turning behaviour of
  tethered membrane wings using automated flight manoeuvres. *Wind Energy Science* 9,
  2261. https://wes.copernicus.org/articles/9/2261/2024/ (turn-rate law, steering gain
  0.15 to 0.35 rad/m, dead time 200 to 600 ms).
- Oehler, J. and Schmehl, R. (2019). Aerodynamic characterization of a soft kite by in situ
  flow measurement. *Wind Energy Science* 4, 1. https://wes.copernicus.org/articles/4/1/2019/
  (L/D 4 powered, 3 depowered, peak 5; angle of attack ranges).
- Fechner, U., van der Vlugt, R., Schreuder, E. and Schmehl, R. (2015). Dynamic model of a
  pumping kite power system. *Renewable Energy* 83, 705-716. https://arxiv.org/abs/1406.6218
  (eq. 72 turn-rate law with `c_1` 0.261 rad/m and `c_2` 6.28 rad m/s^2, steering drag
  `K_s,D` 0.6, tether unit spring constant 614,600 N, lookup-table aerodynamics).
- Hsu, S. A., Meindl, E. A. and Gilhousen, D. B. (1994). Determining the power-law wind-profile
  exponent under near-neutral stability conditions at sea. *Journal of Applied Meteorology* 33,
  757-765. https://journals.ametsoc.org/view/journals/apme/33/6/1520-0450_1994_033_0757_dtplwp_2_0_co_2.xml
  (exponent 0.11 +- 0.03).
- Harper, B. A., Kepert, J. D. and Ginger, J. D. (2010). Guidelines for converting between
  various wind averaging periods in tropical cyclone conditions. WMO/TD-1555.
  https://www.systemsengineeringaustralia.com.au/download/WMO_TC_Wind_Averaging_27_Aug_2010.pdf
  (3 s over 10 min gust factor 1.23 at sea, 1.38 off-sea).
- Savitsky, D. (1964). Hydrodynamic design of planing hulls. *Marine Technology* 1(4), 71-95.
  https://doi.org/10.5957/mt1.1964.1.4.71 (planing lift and drag method).

Standard references, not re-read:

- Loyd, M. L. (1980). Crosswind kite power. *Journal of Energy* 4(3), 106-111.
- Kaimal, J. C., Wyngaard, J. C., Izumi, Y. and Cote, O. R. (1972). Spectral characteristics of
  surface-layer turbulence. *Q. J. R. Meteorol. Soc.* 98, 563-589.
  https://doi.org/10.1002/qj.49709841707
- IEC 61400-1, Wind turbines, Part 1: Design requirements (normal turbulence model, Kaimal
  spectrum parameters, component ratios).
- Taylor, G. I. (1938). The spectrum of turbulence. *Proc. R. Soc. A* 164, 476-490.
- Charnock, H. (1955). Wind stress on a water surface. *Q. J. R. Meteorol. Soc.* 81, 639-640.
- Hoerner, S. F. (1965). *Fluid-Dynamic Drag* (plate normal force at high angles).
- Marchaj, C. A. (1979). *Aero-Hydrodynamics of Sailing* (the course theorem).
- Mueller, M., Heidelberger, B., Hennix, M. and Ratcliff, J. (2007). Position based dynamics.
  *J. Vis. Commun. Image R.* 18, 109-118.
- Macklin, M., Mueller, M. and Chentanez, N. (2016). XPBD: position-based simulation of
  compliant constrained dynamics. *Motion in Games 2016*. https://doi.org/10.1145/2994258.2994272
- Macklin, M., Storey, K., Lu, M., Terdiman, P., Chentanez, N., Jeschke, S. and Mueller, M.
  (2019). Small steps in physics simulation. *SCA 2019*.
- Fiedler, G. Fix Your Timestep! https://gafferongames.com/post/fix_your_timestep/
- Catto, E. (2011). Soft constraints: reinventing the spring. GDC 2011.

From `docs/research.md` (its sources section), used here as given: the WOO height and airtime
data, Simons 2025 landing loads, Cabrinha kite masses and wind ranges, and the line-length
figures.
