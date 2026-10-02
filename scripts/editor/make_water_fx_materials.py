"""Create the translucent materials used by UBoardWakeComponent: M_WaterFoam and M_WaterSpray.

Both are unlit white and take their opacity from per-instance custom data 0, so each foam patch
and spray drop fades on its own. Foam is drawn on flat planes and gets a soft round edge.

Run:  scripts/run-python.sh scripts/editor/make_water_fx_materials.py
"""
import unreal

MATERIAL_DIR = '/Game/Materials'
mel = unreal.MaterialEditingLibrary
asset_tools = unreal.AssetToolsHelpers.get_asset_tools()

# Emissive level that reads as white foam next to sunlit water under the level's 10 lux sun.
BRIGHTNESS = 2.5


def make_material(name, soft_round_edge):
    path = f'{MATERIAL_DIR}/{name}'
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        unreal.EditorAssetLibrary.delete_asset(path)
    material = asset_tools.create_asset(name, MATERIAL_DIR, unreal.Material, unreal.MaterialFactoryNew())
    material.set_editor_property('blend_mode', unreal.BlendMode.BLEND_TRANSLUCENT)
    material.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property('two_sided', True)

    color = mel.create_material_expression(material, unreal.MaterialExpressionConstant3Vector, -600, -200)
    color.set_editor_property('constant', unreal.LinearColor(1.0, 1.0, 1.0, 1.0))
    brightness = mel.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -600, -50)
    brightness.set_editor_property('parameter_name', 'Brightness')
    brightness.set_editor_property('default_value', BRIGHTNESS)
    emissive = mel.create_material_expression(material, unreal.MaterialExpressionMultiply, -350, -150)
    mel.connect_material_expressions(color, '', emissive, 'A')
    mel.connect_material_expressions(brightness, '', emissive, 'B')
    mel.connect_material_property(emissive, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    instance_opacity = mel.create_material_expression(material, unreal.MaterialExpressionPerInstanceCustomData, -600, 150)
    instance_opacity.set_editor_property('data_index', 0)
    instance_opacity.set_editor_property('const_default_value', 1.0)
    opacity = instance_opacity

    if soft_round_edge:
        uv = mel.create_material_expression(material, unreal.MaterialExpressionTextureCoordinate, -1100, 350)
        centre = mel.create_material_expression(material, unreal.MaterialExpressionConstant2Vector, -1100, 500)
        centre.set_editor_property('r', 0.5)
        centre.set_editor_property('g', 0.5)
        distance = mel.create_material_expression(material, unreal.MaterialExpressionDistance, -900, 400)
        mel.connect_material_expressions(uv, '', distance, 'A')
        mel.connect_material_expressions(centre, '', distance, 'B')
        two = mel.create_material_expression(material, unreal.MaterialExpressionConstant, -900, 550)
        two.set_editor_property('r', 2.0)
        scaled = mel.create_material_expression(material, unreal.MaterialExpressionMultiply, -750, 450)
        mel.connect_material_expressions(distance, '', scaled, 'A')
        mel.connect_material_expressions(two, '', scaled, 'B')
        inverted = mel.create_material_expression(material, unreal.MaterialExpressionOneMinus, -600, 450)
        mel.connect_material_expressions(scaled, '', inverted, '')
        mask = mel.create_material_expression(material, unreal.MaterialExpressionSaturate, -450, 450)
        mel.connect_material_expressions(inverted, '', mask, '')
        opacity = mel.create_material_expression(material, unreal.MaterialExpressionMultiply, -250, 250)
        mel.connect_material_expressions(instance_opacity, '', opacity, 'A')
        mel.connect_material_expressions(mask, '', opacity, 'B')

    mel.connect_material_property(opacity, '', unreal.MaterialProperty.MP_OPACITY)
    material.set_editor_property('used_with_instanced_static_meshes', True)
    mel.recompile_material(material)
    unreal.EditorAssetLibrary.save_loaded_asset(material, only_if_is_dirty=False)
    print(f'Created {path}')


make_material('M_WaterFoam', soft_round_edge=True)
make_material('M_WaterSpray', soft_round_edge=False)
print('=== Water FX materials created ===')
