import unreal

print('=== Creating Project Materials for Kite, Board, and Bar ===')

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()

# Base parent material: /Engine/BasicShapes/BasicShapeMaterial
base_mat = unreal.EditorAssetLibrary.load_asset('/Engine/BasicShapes/BasicShapeMaterial')
print('Loaded base material:', base_mat)

mats_to_create = [
    ('M_KiteCanopy', unreal.LinearColor(0.9, 0.2, 0.1, 1.0)),   # Vibrant Red/Orange for kite
    ('M_KiteBoard', unreal.LinearColor(0.05, 0.6, 0.8, 1.0)),   # Cyan/Teal for board
    ('M_ControlBar', unreal.LinearColor(0.1, 0.1, 0.1, 1.0)),   # Dark charcoal/black for bar
    ('M_KiteLines', unreal.LinearColor(0.95, 0.95, 0.95, 1.0)), # Bright white/silver for lines
]

for mat_name, color in mats_to_create:
    dest_path = f'/Game/Materials/{mat_name}'
    if unreal.EditorAssetLibrary.does_asset_exist(dest_path):
        unreal.EditorAssetLibrary.delete_asset(dest_path)
    
    mi_factory = unreal.MaterialInstanceConstantFactoryNew()
    mi = asset_tools.create_asset(mat_name, '/Game/Materials', unreal.MaterialInstanceConstant, mi_factory)
    if mi and base_mat:
        mi.set_editor_property('parent', base_mat)
        # Set Color parameter if parameter exists
        unreal.MaterialEditingLibrary.set_material_instance_vector_parameter_value(mi, 'Color', color)
        unreal.EditorAssetLibrary.save_loaded_asset(mi, only_if_is_dirty=False)
        print(f'Successfully created and saved {mat_name}')

# Assign materials to imported static meshes
mesh_mat_map = [
    ('/Game/Meshes/SM_Kite', '/Game/Materials/M_KiteCanopy'),
    ('/Game/Meshes/SM_KiteBoard', '/Game/Materials/M_KiteBoard'),
    ('/Game/Meshes/SM_ControlBar', '/Game/Materials/M_ControlBar'),
]

for mesh_path, mat_path in mesh_mat_map:
    mesh = unreal.EditorAssetLibrary.load_asset(mesh_path)
    mat = unreal.EditorAssetLibrary.load_asset(mat_path)
    if mesh and mat:
        mesh.set_material(0, mat)
        unreal.EditorAssetLibrary.save_loaded_asset(mesh, only_if_is_dirty=False)
        print(f'Assigned {mat_path} to {mesh_path}')

print('=== Materials Setup Finished ===')
