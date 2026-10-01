import unreal
import os

print('=== Importing Custom Static Meshes for Kite, Board, and Bar ===')

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()

base_dir = '/home/koorik/.hermes/kanban/boards/demo/workspaces/t_96052fbe/kitesurf/scripts/editor/geometry'
items = [
    ('SM_Kite', os.path.join(base_dir, 'kite.obj')),
    ('SM_KiteBoard', os.path.join(base_dir, 'board.obj')),
    ('SM_ControlBar', os.path.join(base_dir, 'control_bar.obj')),
]

for asset_name, obj_path in items:
    task = unreal.AssetImportTask()
    task.set_editor_property('filename', obj_path)
    task.set_editor_property('destination_path', '/Game/Meshes')
    task.set_editor_property('destination_name', asset_name)
    task.set_editor_property('replace_existing', True)
    task.set_editor_property('automated', True)
    task.set_editor_property('save', True)
    
    asset_tools.import_asset_tasks([task])
    loaded = unreal.EditorAssetLibrary.load_asset(f'/Game/Meshes/{asset_name}')
    print(f'Imported {asset_name}: {loaded}')
    if loaded:
        unreal.EditorAssetLibrary.save_loaded_asset(loaded, only_if_is_dirty=False)

print('=== Finished Mesh Imports ===')
