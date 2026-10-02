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

You start planing across the wind with the kite parked low on your right. The kite and the
bar stay where you leave them, so no key needs to be held to keep riding. The same table is
shown in the main menu and the pause menu.

The bar (the kite) is on the arrow keys or the right stick; the board is on WASD or the left stick.

| Action | Keyboard / mouse | Gamepad |
| --- | --- | --- |
| Steer the kite round the window (over the top to change tack) | Left / Right | Right stick left / right |
| Sheet in / out (the bar holds its position) | Up / Down | Right stick up / down, triggers |
| Loop the kite | Hold Shift while steering | Hold right bumper while steering |
| Bar on the mouse | Hold right button: move to steer and sheet, left button loops | |
| Turn the board left / right; spin it in the air | A / D | Left stick left / right |
| Weight on the nose / the tail of the board | W / S | Left stick up / down |
| Jump (pop, with the edge loaded) | Space | Bottom face button |
| Reset | R | Right face button |
| Pause menu (resume, restart, settings, main menu, quit) | Esc or P | Start |

Sending the kite up over your head with the bar in lifts you off the water without a pop, and a
kite looped through the middle of the window pulls several times harder than a parked one.
The wind gusts and drops (the HUD calls out GUST and LULL). The kite only pulls while its lines
are tight: in a hole in the wind, or if you outrun the wind, the lines go slack and the kite
falls until they come tight again. Pull the bar in too far at low speed and it stalls; let it out.

Loop it too low and it goes into the water, where it lies with slack lines until it relaunches
(about three seconds, or sooner if you steer). Weight on the tail (S) digs the rail in and loads
the pop; weight on the nose (W) flattens the board so it runs faster and slides more.

The rider is chosen in Settings: Santa (the default), a wetsuit rider, or the robot.

## Conventions

- **Units**: Unreal cm, cm/s (1 knot = 51.44 cm/s).
- **Water Plane**: World Z = 0.
- **Rendering**: Vulkan SM6, Lumen, Nanite, VSM, TSR.
- **Asset storage**: Large binary files (`*.uasset`, `*.umap`, etc.) are tracked with Git LFS.
