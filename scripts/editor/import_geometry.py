"""Generates and imports the kite, board and control bar meshes, and the kite canopy texture.
The riders are imported by import_rider_parts.py.

    scripts/run-python.sh scripts/editor/import_geometry.py
    scripts/run-python.sh scripts/editor/create_materials.py     # then assign materials

The geometry comes from generate_mesh_objs.py and the texture from make_kite_texture.sh (which
needs ImageMagick); both are written to Saved/Geometry and imported from there.
"""
import os
import subprocess
import sys

import unreal

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, SCRIPT_DIR)
import generate_mesh_objs  # noqa: E402

print('=== Importing meshes and the kite canopy texture ===')

output_dir = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), 'Geometry')
obj_paths = generate_mesh_objs.generate_all(output_dir)

texture_path = os.path.join(output_dir, 'kite_canopy.png')
subprocess.run([os.path.join(SCRIPT_DIR, 'make_kite_texture.sh'), texture_path], check=True)

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()


def import_file(filename, destination_path, destination_name):
    task = unreal.AssetImportTask()
    task.set_editor_property('filename', filename)
    task.set_editor_property('destination_path', destination_path)
    task.set_editor_property('destination_name', destination_name)
    task.set_editor_property('replace_existing', True)
    task.set_editor_property('automated', True)
    task.set_editor_property('save', True)
    asset_tools.import_asset_tasks([task])
    asset = unreal.EditorAssetLibrary.load_asset(f'{destination_path}/{destination_name}')
    if not asset:
        raise RuntimeError(f'Import of {filename} did not produce {destination_path}/{destination_name}')
    unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False)
    print(f'Imported {destination_name}: {asset}')
    return asset


for asset_name, obj_path in obj_paths.items():
    mesh = import_file(obj_path, '/Game/Meshes', asset_name)
    slots = [str(m.get_editor_property('material_slot_name')) for m in mesh.get_editor_property('static_materials')]
    print(f'  {asset_name} material slots: {slots}')

import_file(texture_path, '/Game/Textures', 'T_KiteCanopy')

print('=== Finished imports ===')
