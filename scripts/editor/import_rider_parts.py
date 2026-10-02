"""Generates and imports the jointed riders: a torso and four limb parts for each rider.

    scripts/run-python.sh scripts/editor/import_rider_parts.py

The geometry comes from generate_mesh_objs.generate_rider_parts(), written to Saved/Geometry.
The parts use the rider materials that create_materials.py makes (M_RiderSkin and so on); run
that first on a fresh project. The game poses the parts every frame (RiderRig.cpp).
"""
import os
import sys

import unreal

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, SCRIPT_DIR)
import generate_mesh_objs  # noqa: E402

print('=== Importing jointed rider parts ===')

output_dir = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), 'Geometry')
obj_paths = generate_mesh_objs.generate_rider_parts(output_dir)

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
editor_assets = unreal.EditorAssetLibrary

materials = {}
stray_names = set()
for asset_name, obj_path in obj_paths.items():
    task = unreal.AssetImportTask()
    task.set_editor_property('filename', obj_path)
    task.set_editor_property('destination_path', '/Game/Meshes')
    task.set_editor_property('destination_name', asset_name)
    task.set_editor_property('replace_existing', True)
    task.set_editor_property('automated', True)
    task.set_editor_property('save', True)
    asset_tools.import_asset_tasks([task])
    mesh = editor_assets.load_asset(f'/Game/Meshes/{asset_name}')
    if not mesh:
        raise RuntimeError(f'Import of {obj_path} did not produce /Game/Meshes/{asset_name}')
    for index, slot in enumerate(mesh.get_editor_property('static_materials')):
        slot_name = str(slot.get_editor_property('material_slot_name'))
        if slot_name not in materials:
            material = editor_assets.load_asset(f'/Game/Materials/M_{slot_name}')
            if not material:
                raise RuntimeError(f'{asset_name} slot {index} needs /Game/Materials/M_{slot_name}: run create_materials.py first')
            materials[slot_name] = material
        mesh.set_material(index, materials[slot_name])
        stray_names.add(slot_name)
    editor_assets.save_loaded_asset(mesh, only_if_is_dirty=False)
    print(f'Imported {asset_name}')

# The OBJ importer leaves a material asset per MTL entry next to the meshes; the real ones are in /Game/Materials.
for name in stray_names:
    stray = f'/Game/Meshes/{name}'
    if editor_assets.does_asset_exist(stray):
        editor_assets.delete_asset(stray)

print('=== Finished jointed rider parts ===')
