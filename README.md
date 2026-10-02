# KiteSurf

Kitesurfing game built on Unreal Engine 5.8 with C++ and Enhanced Input.

## Repository Layout

- `Source/KiteSurf/` — Core game module (Runtime)
  - `Public/` & `Private/` — C++ classes: `AKiteRiderPawn`, `UWindComponent`, `AKiteSurfGameMode`
  - `Private/Tests/` — Automation tests (`KiteSurf.Wind.ReturnsBaseWind`, `KiteSurf.Pawn.ClampsInputs`)
- `Config/` — Engine & Input configurations (`DefaultEngine.ini`, `DefaultInput.ini`)
- `Content/` — Unreal assets (maps, meshes, audio)
- `scripts/` — Project workflow scripts:
  - `common.sh` — Engine path resolution and environment variables
  - `build.sh` — Compiles the editor target
  - `run-tests.sh` — Executes automation tests
  - `run-editor.sh` — Launches Unreal Editor with Vulkan + NVIDIA offload
  - `package-linux.sh` — Packages Linux Shipping build via RunUAT
  - `free-resources.sh` — Stop/start host k3s service to free RAM/GPU
  - `setup-engine.sh` — Installs Linux runtime dependencies and extracts engine
- `docs/` — Architecture specifications and task roadmap

## Getting Started

1. **Free system resources** (if host is running background services):
   ```bash
   scripts/free-resources.sh stop
   ```

2. **Install Unreal Engine 5.8** (one-time setup):
   ```bash
   scripts/setup-engine.sh /path/to/Linux_Unreal_Engine_5.7.x.zip /opt/unreal-engine
   ```

3. **Build the project**:
   ```bash
   scripts/build.sh
   ```

4. **Run automation tests**:
   ```bash
   scripts/run-tests.sh -nullrhi
   ```

5. **Launch the editor**:
   ```bash
   scripts/run-editor.sh
   ```

## Playing

```bash
scripts/run-editor.sh -game -windowed -ResX=1600 -ResY=900 -log
```

You start planing across the wind with the kite powered up low on your right. The kite and the
bar stay where you leave them, so no key needs to be held to keep riding.

| Action | Keyboard | Gamepad |
| --- | --- | --- |
| Fly the kite round the window (over the top to change tack) | A / D or Left / Right | Left stick X |
| Sheet in / out (the bar holds its position) | W / S or Up / Down | Right / left trigger |
| Carve the board (hold to keep turning) | Q / E | Left stick Y |
| Jump (while planing with an edge held) | Space | Bottom face button |
| Reset | R | Right face button |
| Pause | Esc | Start |

## Conventions

- **Units**: Unreal cm, cm/s (1 knot = 51.44 cm/s).
- **Water Plane**: World Z = 0.
- **Rendering**: Vulkan SM6, Lumen, Nanite, VSM, TSR.
- **Asset storage**: Large binary files (`*.uasset`, `*.umap`, etc.) are tracked with Git LFS.
