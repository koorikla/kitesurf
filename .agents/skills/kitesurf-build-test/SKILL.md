---
name: kitesurf-build-test
description: Build, test, run and package the KiteSurf Unreal project on Linux.
version: 0.1.0
metadata:
  hermes:
    tags: [kitesurf, unreal-engine, build, ci, linux]
    related_skills: [kitesurf-automation-tests, kitesurf-editor-python, unreal-packaging]
---

# KiteSurf build, test and run

How to compile, test, launch and package this repository from a shell. Everything goes
through `scripts/`; do not call engine binaries with hand-written paths.

## When to Use

- Before reporting any C++ change as done: it must build and the automation tests must pass.
- When CI (`.github/workflows/ci.yml`) fails and you need to reproduce it locally.
- When you need to see the game running, or produce a packaged build.

Do not use this for writing new tests (`kitesurf-automation-tests`) or generating assets
(`kitesurf-editor-python`).

## Procedure

1. **Engine location.** `scripts/common.sh` resolves the engine from `$UE_ROOT`, then a
   `.engine-path` file in the repo root, then `/opt/unreal-engine`. The project targets
   Unreal Engine 5.8 on Linux with Vulkan SM6.
2. **Build the editor target.**
   ```bash
   scripts/build.sh Development
   ```
   This builds `KiteSurfEditor Linux Development`. Extra arguments are passed to the
   engine's `Build.sh`.
3. **Run the automation tests headless.**
   ```bash
   scripts/run-tests.sh -nullrhi
   ```
   This runs every test whose name starts with `KiteSurf` and writes a report to
   `Saved/Automation/Report/index.json`. The last line printed should read
   `Test Results: Total=N, Succeeded=N, Failed=0`.
4. **Launch the game in a window** (needs a display and the GPU):
   ```bash
   scripts/run-editor.sh -game -windowed -ResX=1920 -ResY=1080 -log
   ```
   Without `-game` the same script opens the editor.
5. **Run the GPU smoke test headless** (real Vulkan SM6 rendering; saves
   `Saved/Screenshots/LinuxEditor/smoke.png` 30 frames before exiting, scans the log, and
   fails if either is bad):
   ```bash
   scripts/smoke-test.sh                              # main menu, 600 frames
   scripts/smoke-test.sh /Game/Maps/L_OpenWater 600   # gameplay map
   ```
   Look at the screenshot: a clean log does not prove the ocean is visible.
   To put the game in a particular state for the screenshot, script it with console commands
   in `-ExecCmds` (all registered in `Source/KiteSurf/Private/KiteSurf.cpp`):
   ```bash
   scripts/run-editor.sh /Game/Maps/L_OpenWater -game -RenderOffScreen -ResX=1280 -ResY=720 -log -unattended \
       -ExecCmds="kitesurf.After 500 kitesurf.Input 1 0 0 0 1, kitesurf.SmokeFrames 700"
   ```
   - `kitesurf.After <frames> <command...>` runs a console command later.
   - `kitesurf.Input <steer> <sheet rate> <turn> <weight shift> <raw steer 0|1> [<air rot x> <air rot y> <tuck 0..1>]` holds inputs on the rider. The raw steer flag sends the bar straight to the kite from any position; players loop by steering towards the kite's own side instead. The optional three are the rider's rotation stick in the air (x +1 back roll, y -1 backflip) and the tuck.
   - `kitesurf.Jump`, `kitesurf.TogglePause`, `kitesurf.OpenSettings` and `kitesurf.OpenGear` do what the keys and buttons do.
   - `kitesurf.Wind <knots>` sets the base wind speed, e.g. `kitesurf.After 200 kitesurf.Wind 2` to drop the kite and leave the rider floating.
   - `kite.Physics.Debug 1` draws the winds, forces and numbers at the kite and the rider and a gust bar (colours and scales in `docs/ARCHITECTURE.md`); `kite.Physics.Debug 2` also logs a `kitecsv` line per fixed step to `LogKiteSurf`, which `grep -o 'kitecsv,.*' Saved/Logs/KiteSurf.log | cut -d, -f2-` turns into a CSV. Not in Shipping builds.
   - `kitesurf.Shot <Chase|Side|Low|Orbit|Wide|KiteView>` cuts to a cinematic camera on the rider, `kitesurf.HideUI` clears the HUD and widgets, and `kitesurf.CaptureFrames <frames> <name>` writes every frame to `Saved/MenuVideo/<name>/` and exits. With `-benchmark -fps=30` the timestep is fixed, so the same commands film the same ride every run; `scripts/render-menu-video.sh` uses this to film and encode the menu videos.
   - `kitesurf.Load <0|1>` holds or lets go of the jump button: held is the loaded crouch, letting go pops. `kitesurf.Jump` is an immediate pop.
   - `kitesurf.PreWind <x> <y>` holds the pre-wind stick: it winds up while the jump button is held on the water and the take-off turns it into a rotation (x +1 back roll). A back roll on the default 9 m kite: `kitesurf.After 5 kitesurf.Wind 24, kitesurf.After 5 kitesurf.Input 0.3 0 0 0 0, kitesurf.After 240 kitesurf.Input -1 0 0 -1 0, kitesurf.After 240 kitesurf.Load 1, kitesurf.After 240 kitesurf.PreWind 1 0, kitesurf.After 267 kitesurf.Input 0 1 0 0 0 1 0 1, kitesurf.After 267 kitesurf.Load 0, kitesurf.After 270 kitesurf.PreWind 0 0, kitesurf.After 300 kitesurf.Input 0 0 0 0 0 0 0 0` (the air stick and the tuck for the first second take it over at that kite's line tension; the pre-wind alone turns the rider to horizontal).
   - `kitesurf.MenuKey <key>` sends a key press through the UI (`Down`, `Enter`, `Gamepad_DPad_Up`, `Gamepad_FaceButton_Bottom`, ...) and logs whether a menu handled it, so menu navigation can be driven and captured in an offscreen run.
   - `kitesurf.MotionBar <0|1>` switches the motion-sensor bar and logs the controller, its raw readings and the resulting steer and bar position. It works in offscreen runs if a controller with sensors is connected.
   - `kitesurf.AudioRecordStart` and `kitesurf.AudioRecordStop <name>` record what the game plays, on a ride or in the menus, to `Saved/BouncedWavFiles/<name>.wav`. Offscreen runs use a dummy audio device and are muted as unfocused, so add `-ini:Engine:[Audio]:UnfocusedVolumeMultiplier=1.0`; then check the WAV's level instead of listening.
6. **Package a Linux Shipping build.**
   ```bash
   scripts/package-linux.sh
   ```
   Output is archived under `Build/`.
7. **CI** runs steps 2 and 3 on a self-hosted runner, followed by a non-blocking `gpu-smoke` job (step 5) for every push and pull request to `main`.
   CI builds are incremental: each job keeps `Binaries/`, `Intermediate/` and
   `DerivedDataCache/` from the previous run in the runner's workspace and deletes every other
   untracked file, so `gpu-smoke` reuses the editor that `build-and-test` just built.

## Pitfalls

- **One GPU, one run at a time.** Agents, worktrees and the CI runner share one 8 GB GPU,
  and two Vulkan runs at once can crash with `VulkanMemory.cpp ... Out of memory`. The
  GPU-using scripts therefore take a machine-wide lock
  (`${XDG_RUNTIME_DIR:-/run/user/$UID}/kitesurf-gpu.lock`, via `with_gpu_lock` in
  `scripts/common.sh`) and queue behind each other, printing
  `=== Waiting for the GPU lock ..., held by: pid ... from <worktree> ...` while they wait.
  Taking it: `smoke-test.sh`, `render-menu-video.sh` (per take), `run-editor.sh` with
  `-RenderOffScreen`, `run-python.sh` with `-RenderOffScreen` or
  `-AllowCommandletRendering`, and `run-tests.sh` without `-nullrhi`. Not taking it: a
  windowed `run-editor.sh` (editor or `-game`), because someone is at the screen and it may
  stay open for hours, blocking every queued run; close it before GPU runs if VRAM is tight.
  Call the engine through these scripts, not directly, or the lock is bypassed.
  `KITESURF_GPU_LOCK=0` skips the lock; `KITESURF_GPU_LOCK_FILE` points it elsewhere.
  A wait is not a hang: check the holder's pid before killing anything.
- **The lock only queues runs that take it.** Branches from before the lock, direct engine
  calls and windowed runs still hold VRAM; a 1280x720 smoke run needs about 3.5 GB, so it
  crashes at frame 0 next to any other offscreen game. With the lock held, a command
  therefore also waits for `KITESURF_GPU_MIN_FREE_MB` (4096) of free VRAM, for up to
  `KITESURF_GPU_WAIT_SECONDS` (900), printing `=== Waiting for 4096 MB of free VRAM ...` and
  the engine processes on the GPU. `smoke-test.sh` also retries a run that died with
  `Fatal error: [File:...VulkanMemory.cpp]` up to `KITESURF_SMOKE_OOM_RETRIES` (2) times.
  A red `gpu-smoke` with that error is contention, not a code bug: see who held the GPU in the
  log before blaming the change. Rebase old branches so their runs take the lock.
- **A green test run can be empty.** `scripts/run-tests.sh` only warns when the report is
  missing, and `scripts/parse_test_report.py` exits 0 if the report cannot be parsed. Always
  read the `Test Results:` line and check that `Total` is the number of tests you expect.
- **Header or `UPROPERTY` changes need a full build**, not Live Coding.
- **`-nullrhi` has no renderer.** Tests that need rendering must be flagged `NonNullRHI`
  (see `kitesurf-automation-tests`) or they will fail or silently do nothing.
- **A cook or `-nullrhi` log saying "Ray tracing is disabled. Reason: not supported by
  current RHI" is expected.** It says nothing about the real game; check a `-game` run.
- **Headless `-RenderOffScreen` runs of the editor hit `VK_ERROR_DEVICE_LOST`** in the editor-only selection outline pass. Always use `-game` mode (not editor) for off-screen rendering checks and CI smoke tests. If Vulkan device loss ever occurs on specific hardware or driver configurations, apply the following mitigations:
  - Add `-NoRaytracing` to bypass hardware ray tracing pipeline initialization.
  - Disable async compute: `-ExecCmds="r.Vulkan.AllowAsyncCompute=0, r.RDG.AsyncCompute=0"`.
  - On the `koorikla` runner (NVIDIA RTX 4070 Ti SUPER, Linux 7.2 Vulkan SM6), standard `-game -RenderOffScreen` executes without requiring these flags.
- **Binary assets are in Git LFS** (`.uasset`, `.umap`, textures, audio). A checkout
  without LFS content builds but fails at runtime.
- **Logs** are in `Saved/Logs/KiteSurf.log`; each run rotates the previous log to a
  `KiteSurf-backup-*.log` file. Make sure you are reading the run you care about.
- **Config changes live in `Config/DefaultEngine.ini`.** A running game only reflects the
  checkout it was started from.

## Verification

- `scripts/build.sh Development` exits 0.
- `scripts/run-tests.sh -nullrhi` prints `Failed=0` and the expected `Total`.
- For anything visual, state plainly whether you saw it in a `-game` run or only inferred
  it from logs.
