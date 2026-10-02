# Development Tasks Roadmap

## Phase 1 — Playable prototype (DONE, 2026-10-02)

All six feature PRs are merged into `main` (#6 wind, #4 level, #3 board, #2 kite,
#5 input/camera/HUD, #1 CI). `main` builds (Editor + Game, UE 5.8.3 Linux),
17/17 `KiteSurf.*` automation tests pass on the RTX 5060 (Vulkan), the game runs
on `L_OpenWater`, CI is green on the self-hosted runner.

- [x] Environment: UE 5.8 at `/opt/unreal-engine`, Vulkan SM6, `scripts/*.sh`.
- [x] Wind: `UWindComponent` gust field (Perlin), direction drift, log shear; `UKiteWindMath` shared helpers.
- [x] Kite aerodynamics: `UKiteComponent` lift/drag, line tension, wind-window dynamics, depower drift.
- [x] Board hydrodynamics: `UBoardMovementComponent` buoyancy, displacement/planing drag, edge grip; owns all velocity integration (`AddExternalForce`).
- [x] Input & camera: Enhanced Input (`IA_Steer`, `IA_Sheet`, `IA_Edge`, `IMC_Default`), `BP_KiteRider` chase camera.
- [x] HUD: `AKiteSurfHUD` speed (knots), wind compass, wind-window arc, FPS.
- [x] Level: `L_OpenWater` (Water plugin ocean, sun, sky atmosphere, fog, volumetric clouds) via `scripts/editor/make_open_water_level.py`.
- [x] CI: self-hosted runner `koorikla` (`scripts/ci/`), `.github/workflows/ci.yml` builds + runs tests on push/PR.

Integration seams resolved on merge:
- `AKiteRiderPawn::Tick` only does `BoardMovement->AddExternalForce(Kite->GetLineForce())`; no inline integration.
- `UKiteComponent` uses `UKiteWindMath::ApparentWind` / `KitePositionInWindow` (no local duplicates).
- `IA_Edge` is bound to `AKiteRiderPawn::EdgeAction` on `BP_KiteRider` (asserted in `KiteSurf.Input.AssetsValid`).
- `BP_KiteSurfGameMode` spawns `BP_KiteRider` which carries Kite + BoardMovement components (asserted in the same test).

## Phase 2 — Candidates (not yet scheduled; do not start without a card)

Known gaps from the phase-1 play-test (operator note): the running game currently
shows no visible water surface, rider, board, kite, or clouds — the simulation
runs but has essentially no visuals. The first three items below address that.

1. [x] **Visible rider, board and kite meshes** (DONE, PR #10) — `BoardMesh` has no static mesh assigned; there is no kite mesh or line rendering (only `bDrawDebug` lines). Add placeholder meshes (board plank, kite canopy following `UKiteComponent::GetKiteWorldPosition`, line segments) and a simple rider capsule.
2. **Water shader / foam / visible ocean** (ocean renders in `-game`: level regenerated with a renderer, 100 km ocean with a camera-following water mesh, calmer waves; `UBoardWakeComponent` adds a foam trail and spray; wind ripples still open) — verify `WaterBodyOcean` + `WaterZone` actually render in `-game` (material, Lumen/RT settings, `r.Water.*`), add foam/wake behind the board, surface ripples as wind cue.
3. **Sky & volumetric clouds visibility** (horizon reads as sea + sky in `-game` after the fog and camera fixes; Shipping not checked) — confirm `VolumetricCloud` / `SkyAtmosphere` render in Shipping; tune sun angle and cloud coverage so the horizon reads as sea + sky.
4. [x] **Jumps + hangtime** (DONE, PR #11) — allow the board to leave the water when line tension spikes with the kite overhead; air control, landing impact, HUD airtime counter.
5. **Rider animation** — skeletal rider with IK legs on the board, harness/bar pose driven by sheet input, lean driven by edge input.
6. **Course / race mode** — buoy course on `L_OpenWater`, lap timer, checkpoints, best-time persistence; `AKiteSurfGameMode` state machine.
7. [x] **Audio** (first version) — wind, water and line-hum loops faded and pitched by apparent wind, board speed and line tension; pop, landing, crash and reset one-shots. Synthesised by `scripts/editor/make_sound_assets.py`. Still to do: kite flutter when depowered, UI ticks, a mix with buses and ducking.
9. **Spot** (first version done) — sandbars, islands and sharks from `AKiteSurfSpot`, toggled on the gear screen. Still to do: a map editor, shallow water that slows the board, waves and kickers, sharks that avoid sand.
8. **Packaging follow-ups** — `scripts/package-linux.sh` needs a writable `Engine/Programs/AutomationTool` under `/opt/unreal-engine` (fixed on this host by chown; make the install script do it) and a `-nullrhi`-free smoke run of the Shipping binary in CI.
9. [x] **Menu video** (first version) — a pre-rendered intro (title over the rider's big jump) and a 24 s menu background loop, filmed in the game by `scripts/render-menu-video.sh` with cinematic cameras on a scripted ride, played with Electra/VP9; the still art is the fallback; loading screen for the ride. Still to do: 3D previews of rider, kite and board on the gear screen; menu music.
