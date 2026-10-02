"""Synthesises the game's sound effects and music and imports them as SoundWave assets in /Game/Audio.

    scripts/run-python.sh scripts/editor/make_sound_assets.py

The effects come from make_sound_wavs.py (SW_*) and the music from make_music_wavs.py (MU_*),
written to Saved/GeneratedAudio and imported from there. The loops are marked looping and set to
keep playing while silent: the game fades them right down (no wind, board in the air, the
music's in-the-air layer on the water) and they must come back, in step.
"""
import os
import sys

import unreal

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, SCRIPT_DIR)
import make_music_wavs  # noqa: E402
import make_sound_wavs  # noqa: E402

print('=== Generating and importing sounds ===')

output_dir = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), 'GeneratedAudio')
wav_paths = make_sound_wavs.generate_all(output_dir)
wav_paths.update(make_music_wavs.generate_all(output_dir))
loops = set(make_sound_wavs.LOOPS) | set(make_music_wavs.TRACKS)

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
editor_assets = unreal.EditorAssetLibrary

for asset_name, wav_path in wav_paths.items():
    task = unreal.AssetImportTask()
    task.set_editor_property('filename', wav_path)
    task.set_editor_property('destination_path', '/Game/Audio')
    task.set_editor_property('destination_name', asset_name)
    task.set_editor_property('replace_existing', True)
    task.set_editor_property('automated', True)
    task.set_editor_property('save', True)
    asset_tools.import_asset_tasks([task])

    wave = editor_assets.load_asset(f'/Game/Audio/{asset_name}')
    if not wave:
        raise RuntimeError(f'Import of {wav_path} did not produce /Game/Audio/{asset_name}')
    is_loop = asset_name in loops
    wave.set_editor_property('looping', is_loop)
    if is_loop:
        wave.set_editor_property('virtualization_mode', unreal.VirtualizationMode.PLAY_WHEN_SILENT)
    editor_assets.save_loaded_asset(wave, only_if_is_dirty=False)
    print(f'Imported {asset_name}: {wave.get_editor_property("duration"):.2f} s, looping {is_loop}')

# The empty MetaSounds these replace
for stale in ['MS_AudioBed', 'MS_Pop', 'MS_Landing', 'MS_Crash', 'MS_ResetCue']:
    path = f'/Game/Audio/{stale}'
    if editor_assets.does_asset_exist(path):
        editor_assets.delete_asset(path)
        print(f'Deleted {path}')

print('=== Finished sounds ===')
