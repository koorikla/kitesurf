---
name: kitesurf-editor-python
description: Generate KiteSurf levels and assets with Unreal editor Python.
version: 0.1.0
metadata:
  hermes:
    tags: [kitesurf, unreal-engine, python, assets, levels]
    related_skills: [kitesurf-build-test, unreal-enhanced-input, unreal-niagara]
---

# KiteSurf editor Python scripts

Unreal assets (`.uasset`, `.umap`) are binary and cannot be edited as text. In this
repository they are produced by Python scripts under `scripts/editor/`, run inside the
editor without a GUI. The script is the source of truth; the binary is its output.

## When to Use

- Creating or changing a level, an Input Action, an Input Mapping Context, a Blueprint
  default, or any other asset.
- Placing actors in `L_OpenWater` (meshes, kickers, lights, water bodies).
- Checking engine or project state from inside the editor (console variables, loaded
  assets).

For C++ classes use `unreal-cpp-gameplay`; for the input model use `unreal-enhanced-input`.

## Procedure

1. **Find the existing script for the asset** before writing a new one:
   - `scripts/editor/make_open_water_level.py` builds `/Game/Maps/L_OpenWater`.
   - `scripts/editor/make_input_assets.py` builds `IA_*`, `IMC_Default` and `BP_KiteRider`
     defaults.
   Extend the existing script so one run still rebuilds the whole asset.
2. **Write the script against the `unreal` module** and make it idempotent: load the asset
   if it exists, create it if not, then set every property explicitly.
   ```python
   import unreal

   def get_or_create_asset(name, package_path, asset_class, factory=None):
       path = f"{package_path}/{name}"
       if unreal.EditorAssetLibrary.does_asset_exist(path):
           return unreal.EditorAssetLibrary.load_asset(path)
       tools = unreal.AssetToolsHelpers.get_asset_tools()
       return tools.create_asset(name, package_path, asset_class, factory)
   ```
3. **Save what you changed** (`unreal.EditorAssetLibrary.save_asset(path, False)`, or save
   the level) and print a clear line per asset so the log shows what happened.
4. **Run it with the engine's Python commandlet.** Source `scripts/common.sh` for the
   engine paths:
   ```bash
   source scripts/common.sh
   "$UE_EDITOR_CMD" "$UPROJECT" -run=pythonscript \
       -Script="$PROJECT_ROOT/scripts/editor/make_input_assets.py" \
       -unattended -nosplash -stdout
   ```
   `PythonScriptPlugin` and `EditorScriptingUtilities` are enabled for the editor in
   `KiteSurf.uproject`.
5. **Assert the result in an automation test**, as `KiteSurf.Input.AssetsValid` does for
   the input assets, so CI notices when a script and its asset drift apart.
6. **Commit the script and the generated binary together.** Binaries go through Git LFS.

## Pitfalls

- **Editing only the binary.** A change made by hand in the editor is lost the next time
  the script runs. Change the script.
- **Non-idempotent scripts.** A script that appends on every run duplicates actors or
  mappings. Clear and rebuild, or look up before creating.
- **Absolute paths.** Do not hardcode a home directory; one existing script writes to
  `/home/<user>/...` and breaks in other checkouts. Build paths from
  `unreal.Paths.project_saved_dir()` or `unreal.Paths.project_dir()`.
- **A script error does not always fail the process.** Read the log for Python tracebacks
  and for the lines you printed; do not rely on the exit code alone.
- **The commandlet has no renderer.** Anything that needs a viewport (screenshots, ray
  tracing state) will not behave as it does in the game. Verify visuals in a `-game` run.
- **The Water plugin is experimental** in 5.8. Property names on water actors change
  between versions; confirm them against the engine source, not from memory.

## Verification

- The script's printed lines appear in the log with no traceback.
- `git status` shows the expected asset files changed, and only those.
- The automation test covering the asset passes.
- For level changes, a `-game` run shows the result; say so explicitly, or say it was not
  checked.
