"""Generates and imports the spot scenery (island, sandbar, shark) and gives it materials.

    scripts/run-python.sh scripts/editor/import_spot_assets.py

The geometry comes from generate_mesh_objs.generate_spot(), written to Saved/Geometry. This is
separate from import_geometry.py so that changing the scenery does not re-import the kite, board
and riders.
"""
import os
import sys

import unreal

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, SCRIPT_DIR)
import generate_mesh_objs  # noqa: E402

print('=== Importing spot scenery ===')

output_dir = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), 'Geometry')
obj_paths = generate_mesh_objs.generate_spot(output_dir)

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
editor_assets = unreal.EditorAssetLibrary


def make_flat(name, color, roughness):
    path = f'/Game/Materials/{name}'
    if editor_assets.does_asset_exist(path):
        editor_assets.delete_asset(path)
    material = asset_tools.create_asset(name, '/Game/Materials', unreal.Material, unreal.MaterialFactoryNew())
    base = unreal.MaterialEditingLibrary.create_material_expression(material, unreal.MaterialExpressionConstant3Vector, -300, 0)
    base.set_editor_property('constant', color)
    unreal.MaterialEditingLibrary.connect_material_property(base, '', unreal.MaterialProperty.MP_BASE_COLOR)
    rough = unreal.MaterialEditingLibrary.create_material_expression(material, unreal.MaterialExpressionConstant, -300, 200)
    rough.set_editor_property('r', roughness)
    unreal.MaterialEditingLibrary.connect_material_property(rough, '', unreal.MaterialProperty.MP_ROUGHNESS)
    material.set_editor_property('two_sided', True)
    # Nanite is on project-wide, so the imported meshes are Nanite; -game cannot add the flag itself.
    material.set_editor_property('used_with_nanite', True)
    unreal.MaterialEditingLibrary.recompile_material(material)
    editor_assets.save_loaded_asset(material, only_if_is_dirty=False)
    print(f'Created {name}')
    return material


materials = {
    'Sand': make_flat('M_Sand', unreal.LinearColor(0.72, 0.62, 0.40, 1.0), 0.95),
    'PalmTrunk': make_flat('M_PalmTrunk', unreal.LinearColor(0.20, 0.13, 0.07, 1.0), 0.9),
    'PalmLeaf': make_flat('M_PalmLeaf', unreal.LinearColor(0.05, 0.22, 0.06, 1.0), 0.7),
    'Shark': make_flat('M_Shark', unreal.LinearColor(0.16, 0.19, 0.22, 1.0), 0.45),
    'Dolphin': make_flat('M_Dolphin', unreal.LinearColor(0.40, 0.45, 0.50, 1.0), 0.4),
    'Seagull': make_flat('M_Seagull', unreal.LinearColor(0.85, 0.85, 0.85, 1.0), 0.8),
    'Beak': make_flat('M_Beak', unreal.LinearColor(0.8, 0.6, 0.1, 1.0), 0.6),
    'Rock': make_flat('M_Rock', unreal.LinearColor(0.25, 0.25, 0.25, 1.0), 0.9),
    'Coral': make_flat('M_Coral', unreal.LinearColor(0.7, 0.2, 0.2, 1.0), 0.7),
}

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
            raise RuntimeError(f'{asset_name} slot {index} is named {slot_name!r}, which has no material')
        mesh.set_material(index, materials[slot_name])
        print(f'{asset_name}[{index}] {slot_name}')
    editor_assets.save_loaded_asset(mesh, only_if_is_dirty=False)

# The OBJ importer leaves a material asset per MTL entry next to the meshes; ours replace them.
for name in materials:
    stray = f'/Game/Meshes/{name}'
    if editor_assets.does_asset_exist(stray):
        editor_assets.delete_asset(stray)

print('=== Finished spot scenery ===')
