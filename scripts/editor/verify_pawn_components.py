import unreal

print('=== Verifying BP_KiteRider Components and Defaults ===')
bp = unreal.EditorAssetLibrary.load_asset('/Game/Blueprints/BP_KiteRider')
print('BP loaded:', bp)

cdo = unreal.get_default_object(bp.generated_class())
print('CDO:', cdo)

for prop_name in ['board_mesh', 'rider_mesh', 'control_bar_mesh', 'kite', 'camera_boom', 'follow_camera']:
    val = cdo.get_editor_property(prop_name)
    print(f'CDO {prop_name}: {val}')
    if hasattr(val, 'get_editor_property'):
        try:
            if 'mesh' in prop_name:
                print(f'   mesh asset: {val.get_editor_property("static_mesh")}')
        except Exception:
            pass
        try:
            if 'rider' in prop_name:
                print(f'   skeletal mesh: {val.get_editor_property("skeletal_mesh_asset")}')
                print(f'   anim: {val.get_editor_property("animation_data")}')
        except Exception:
            pass

print('=== Verification Complete ===')
