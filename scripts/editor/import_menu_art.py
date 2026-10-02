"""Draws the startup splash and the menu background, and imports the background as a texture.

    scripts/run-python.sh scripts/editor/import_menu_art.py

make_splash.sh (which needs ImageMagick) writes the splash straight into Content/Splash, where the
engine reads it as a loose file, and the menu background into Saved/MenuArt, from where it is
imported as /Game/Textures/T_MenuBackground for the menus to draw.
"""
import os
import subprocess

import unreal

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))

print('=== Drawing and importing menu art ===')

art_dir = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), 'MenuArt')
subprocess.run([os.path.join(SCRIPT_DIR, 'make_splash.sh'), art_dir], check=True)

task = unreal.AssetImportTask()
task.set_editor_property('filename', os.path.join(art_dir, 'menu_background.png'))
task.set_editor_property('destination_path', '/Game/Textures')
task.set_editor_property('destination_name', 'T_MenuBackground')
task.set_editor_property('replace_existing', True)
task.set_editor_property('automated', True)
task.set_editor_property('save', True)
unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])

texture = unreal.EditorAssetLibrary.load_asset('/Game/Textures/T_MenuBackground')
if not texture:
    raise RuntimeError('Import did not produce /Game/Textures/T_MenuBackground')
# Drawn on screen at close to its own size: no mips, no streaming, UI compression.
# Colours as drawn: sRGB, and not block-compressed.
texture.set_editor_property('srgb', True)
texture.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_EDITOR_ICON)
texture.set_editor_property('lod_group', unreal.TextureGroup.TEXTUREGROUP_UI)
texture.set_editor_property('mip_gen_settings', unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
texture.set_editor_property('never_stream', True)
unreal.EditorAssetLibrary.save_loaded_asset(texture, only_if_is_dirty=False)
print(f'Imported T_MenuBackground: {texture.blueprint_get_size_x()} x {texture.blueprint_get_size_y()}, sRGB {texture.get_editor_property("srgb")}')
print('=== Finished menu art ===')
