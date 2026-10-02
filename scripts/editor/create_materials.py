"""Creates the materials for the kite, board, bar and riders and assigns them to the mesh slots.

Run after import_geometry.py:  scripts/run-python.sh scripts/editor/create_materials.py
"""
import unreal

print('=== Creating project materials ===')

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
mel = unreal.MaterialEditingLibrary
MATERIAL_DIR = '/Game/Materials'

base_mat = unreal.EditorAssetLibrary.load_asset('/Engine/BasicShapes/BasicShapeMaterial')


def recreate(name, asset_class, factory):
    path = f'{MATERIAL_DIR}/{name}'
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        unreal.EditorAssetLibrary.delete_asset(path)
    return asset_tools.create_asset(name, MATERIAL_DIR, asset_class, factory)


def make_flat(name, color):
    """A plain coloured instance of the engine's basic shape material."""
    instance = recreate(name, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    instance.set_editor_property('parent', base_mat)
    mel.set_material_instance_vector_parameter_value(instance, 'Color', color)
    unreal.EditorAssetLibrary.save_loaded_asset(instance, only_if_is_dirty=False)
    print(f'Created {name}')
    return instance


def make_kite_canopy(name='M_KiteCanopy', texture_name='T_KiteCanopy'):
    """Two-sided cloth carrying the canopy texture, so the kite reads from above and from the rider's side."""
    texture = unreal.EditorAssetLibrary.load_asset(f'/Game/Textures/{texture_name}')
    if not texture:
        raise RuntimeError(f'{texture_name} is missing: run import_geometry.py first')
    material = recreate(name, unreal.Material, unreal.MaterialFactoryNew())
    material.set_editor_property('two_sided', True)
    sample = mel.create_material_expression(material, unreal.MaterialExpressionTextureSample, -500, 0)
    sample.set_editor_property('texture', texture)
    mel.connect_material_property(sample, 'RGB', unreal.MaterialProperty.MP_BASE_COLOR)
    roughness = mel.create_material_expression(material, unreal.MaterialExpressionConstant, -300, 250)
    roughness.set_editor_property('r', 0.55)
    mel.connect_material_property(roughness, '', unreal.MaterialProperty.MP_ROUGHNESS)
    mel.recompile_material(material)
    unreal.EditorAssetLibrary.save_loaded_asset(material, only_if_is_dirty=False)
    print(f'Created {name}')
    return material


materials = {
    'KiteCanopy': make_kite_canopy(),
    'KiteCanopyBoost': make_kite_canopy('M_KiteCanopyBoost', 'T_KiteCanopyBoost'),
    'KiteCanopyWave': make_kite_canopy('M_KiteCanopyWave', 'T_KiteCanopyWave'),
    'KiteCanopyFreestyle': make_kite_canopy('M_KiteCanopyFreestyle', 'T_KiteCanopyFreestyle'),
    'KiteTube': make_flat('M_KiteTube', unreal.LinearColor(0.9, 0.9, 0.88, 1.0)),
    'KiteBoard': make_flat('M_KiteBoard', unreal.LinearColor(0.05, 0.6, 0.8, 1.0)),
    'ControlBar': make_flat('M_ControlBar', unreal.LinearColor(0.1, 0.1, 0.1, 1.0)),
    'RiderSkin': make_flat('M_RiderSkin', unreal.LinearColor(0.80, 0.50, 0.38, 1.0)),
    'RiderRed': make_flat('M_RiderRed', unreal.LinearColor(0.70, 0.03, 0.04, 1.0)),
    'RiderWhite': make_flat('M_RiderWhite', unreal.LinearColor(0.92, 0.92, 0.92, 1.0)),
    'RiderBlack': make_flat('M_RiderBlack', unreal.LinearColor(0.02, 0.02, 0.02, 1.0)),
    'RiderWetsuit': make_flat('M_RiderWetsuit', unreal.LinearColor(0.03, 0.04, 0.06, 1.0)),
    'RiderAccent': make_flat('M_RiderAccent', unreal.LinearColor(0.05, 0.6, 0.8, 1.0)),
    'RiderRobotMetal': make_flat('M_RiderRobotMetal', unreal.LinearColor(0.5, 0.5, 0.55, 1.0)),
    'RiderRobotDark': make_flat('M_RiderRobotDark', unreal.LinearColor(0.15, 0.15, 0.18, 1.0)),
    'RiderRobotAccent': make_flat('M_RiderRobotAccent', unreal.LinearColor(0.0, 0.8, 1.0, 1.0)),
}
make_flat('M_KiteLines', unreal.LinearColor(0.95, 0.95, 0.95, 1.0))

# Meshes whose slots are named after the materials above; the board and bar have a single unnamed slot.
SINGLE_SLOT = {'SM_KiteBoard': 'KiteBoard', 'SM_ControlBar': 'ControlBar'}
for mesh_name in ['SM_Kite', 'SM_KiteBoost', 'SM_KiteWave', 'SM_KiteFreestyle', 'SM_KiteBoard', 'SM_ControlBar', 'SM_RiderSanta', 'SM_RiderWetsuit', 'SM_RiderRobot']:
    mesh = unreal.EditorAssetLibrary.load_asset(f'/Game/Meshes/{mesh_name}')
    if not mesh:
        raise RuntimeError(f'{mesh_name} is missing: run import_geometry.py first')
    slots = mesh.get_editor_property('static_materials')
    for index, slot in enumerate(slots):
        slot_name = str(slot.get_editor_property('material_slot_name'))
        key = SINGLE_SLOT.get(mesh_name, slot_name)
        if key not in materials:
            raise RuntimeError(f'{mesh_name} slot {index} is named {slot_name!r}, which has no material')
        mesh.set_material(index, materials[key])
        print(f'{mesh_name}[{index}] {slot_name} -> {key}')
    unreal.EditorAssetLibrary.save_loaded_asset(mesh, only_if_is_dirty=False)

# The OBJ importer leaves a material asset per MTL entry next to the meshes; ours replace them.
for name in list(materials.keys()):
    stray = f'/Game/Meshes/{name}'
    if unreal.EditorAssetLibrary.does_asset_exist(stray):
        unreal.EditorAssetLibrary.delete_asset(stray)
        print(f'Removed imported material {stray}')

print('=== Materials setup finished ===')
