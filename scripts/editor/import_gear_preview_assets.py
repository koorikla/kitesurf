"""Generates and imports what the gear screen's preview stand needs besides the gear itself:
the backdrop card and its sky-and-sea gradient.

    scripts/run-python.sh scripts/editor/import_gear_preview_assets.py

The card comes from generate_mesh_objs.generate_preview(), written to Saved/Geometry. The rider,
kite and board meshes are the ones on the water (import_geometry.py).
"""
import os
import sys

import unreal

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, SCRIPT_DIR)
import generate_mesh_objs  # noqa: E402

print('=== Importing gear preview assets ===')

output_dir = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), 'Geometry')
obj_paths = generate_mesh_objs.generate_preview(output_dir)

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
editor_assets = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary

# Sky above a horizon about 60 % of the way down, sea below it; the same colours as the menu art.
SKY_TOP = unreal.LinearColor(0.10, 0.30, 0.66, 1.0)
SKY_HORIZON = unreal.LinearColor(0.62, 0.80, 0.92, 1.0)
SEA_TOP = unreal.LinearColor(0.05, 0.42, 0.52, 1.0)
SEA_BOTTOM = unreal.LinearColor(0.01, 0.12, 0.22, 1.0)
HORIZON_V = 0.62


def make_backdrop_material():
    """Unlit, so the stand's lights do not touch it; Brightness is a parameter to match the camera's exposure."""
    name = 'M_PreviewBackdrop'
    path = f'/Game/Materials/{name}'
    if editor_assets.does_asset_exist(path):
        editor_assets.delete_asset(path)
    material = asset_tools.create_asset(name, '/Game/Materials', unreal.Material, unreal.MaterialFactoryNew())
    material.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property('two_sided', True)

    def node(cls, x, y):
        return mel.create_material_expression(material, cls, x, y)

    def constant(value, x, y):
        c = node(unreal.MaterialExpressionConstant, x, y)
        c.set_editor_property('r', value)
        return c

    def colour(value, x, y):
        c = node(unreal.MaterialExpressionConstant3Vector, x, y)
        c.set_editor_property('constant', value)
        return c

    def binary(cls, a, b, x, y):
        n = node(cls, x, y)
        mel.connect_material_expressions(a, '', n, 'A')
        mel.connect_material_expressions(b, '', n, 'B')
        return n

    def saturate(a, x, y):
        n = node(unreal.MaterialExpressionSaturate, x, y)
        mel.connect_material_expressions(a, '', n, '')
        return n

    def lerp(a, b, alpha, x, y):
        n = node(unreal.MaterialExpressionLinearInterpolate, x, y)
        mel.connect_material_expressions(a, '', n, 'A')
        mel.connect_material_expressions(b, '', n, 'B')
        mel.connect_material_expressions(alpha, '', n, 'Alpha')
        return n

    uv = node(unreal.MaterialExpressionTextureCoordinate, -1400, 0)
    v = node(unreal.MaterialExpressionComponentMask, -1250, 0)
    v.set_editor_property('r', False)
    v.set_editor_property('g', True)
    mel.connect_material_expressions(uv, '', v, '')

    horizon = constant(HORIZON_V, -1250, 150)
    sky_t = saturate(binary(unreal.MaterialExpressionDivide, v, horizon, -1100, 0), -950, 0)
    sky = lerp(colour(SKY_TOP, -950, -250), colour(SKY_HORIZON, -950, -150), sky_t, -800, -150)

    below = binary(unreal.MaterialExpressionSubtract, v, horizon, -1100, 200)
    sea_t = saturate(binary(unreal.MaterialExpressionDivide, below, constant(1.0 - HORIZON_V, -1250, 300), -950, 200), -800, 200)
    sea = lerp(colour(SEA_TOP, -800, 350), colour(SEA_BOTTOM, -800, 450), sea_t, -650, 300)

    # A sharp but not aliased horizon.
    edge = saturate(binary(unreal.MaterialExpressionMultiply, below, constant(120.0, -950, 450), -800, 550), -650, 550)
    sky_and_sea = lerp(sky, sea, edge, -450, 100)

    brightness = node(unreal.MaterialExpressionScalarParameter, -450, 300)
    brightness.set_editor_property('parameter_name', 'Brightness')
    brightness.set_editor_property('default_value', 1.0)
    lit = binary(unreal.MaterialExpressionMultiply, sky_and_sea, brightness, -250, 150)
    mel.connect_material_property(lit, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    # Nanite is on project-wide, so the card is a Nanite mesh; -game cannot add the flag itself.
    material.set_editor_property('used_with_nanite', True)
    mel.recompile_material(material)
    editor_assets.save_loaded_asset(material, only_if_is_dirty=False)
    print(f'Created {name}')
    return material


materials = {'PreviewBackdrop': make_backdrop_material()}

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

for name in materials:
    stray = f'/Game/Meshes/{name}'
    if editor_assets.does_asset_exist(stray):
        editor_assets.delete_asset(stray)

print('=== Finished gear preview assets ===')
