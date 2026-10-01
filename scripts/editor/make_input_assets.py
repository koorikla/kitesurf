import unreal
import sys
import traceback

def get_or_create_asset(asset_name, package_path, asset_class, factory=None):
    full_path = f'{package_path}/{asset_name}'
    editor_asset_lib = unreal.EditorAssetLibrary
    if editor_asset_lib.does_asset_exist(full_path):
        asset = editor_asset_lib.load_asset(full_path)
        if asset:
            return asset
    
    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    asset = asset_tools.create_asset(asset_name, package_path, asset_class, factory)
    return asset

def build_assets():
    try:
        editor_asset_lib = unreal.EditorAssetLibrary

        print('=== Creating Input Actions ===')
        # 1. IA_Steer (Axis1D)
        ia_steer = get_or_create_asset('IA_Steer', '/Game/Input', unreal.InputAction, None)
        ia_steer.set_editor_property('value_type', unreal.InputActionValueType.AXIS1D)
        editor_asset_lib.save_loaded_asset(ia_steer)
        editor_asset_lib.save_asset('/Game/Input/IA_Steer', False)
        print(f'IA_Steer configured and saved: {ia_steer}')

        # 2. IA_Sheet (Axis1D)
        ia_sheet = get_or_create_asset('IA_Sheet', '/Game/Input', unreal.InputAction, None)
        ia_sheet.set_editor_property('value_type', unreal.InputActionValueType.AXIS1D)
        editor_asset_lib.save_loaded_asset(ia_sheet)
        editor_asset_lib.save_asset('/Game/Input/IA_Sheet', False)
        print(f'IA_Sheet configured and saved: {ia_sheet}')

        # 3. IA_Edge (Axis1D)
        ia_edge = get_or_create_asset('IA_Edge', '/Game/Input', unreal.InputAction, None)
        ia_edge.set_editor_property('value_type', unreal.InputActionValueType.AXIS1D)
        editor_asset_lib.save_loaded_asset(ia_edge)
        editor_asset_lib.save_asset('/Game/Input/IA_Edge', False)
        print(f'IA_Edge configured and saved: {ia_edge}')

        print('=== Creating Input Mapping Context ===')
        imc = get_or_create_asset('IMC_Default', '/Game/Input', unreal.InputMappingContext, None)
        imc.unmap_all()

        # Map Steer
        # Key D (+1)
        key_d = unreal.Key()
        key_d.import_text('D')
        imc.map_key(ia_steer, key_d)

        # Key A (-1 via Negate)
        key_a = unreal.Key()
        key_a.import_text('A')
        map_a = imc.map_key(ia_steer, key_a)
        neg_a = unreal.new_object(unreal.InputModifierNegate, outer=imc)
        map_a.set_editor_property('modifiers', [neg_a])

        # Key Right (+1)
        key_right = unreal.Key()
        key_right.import_text('Right')
        imc.map_key(ia_steer, key_right)

        # Key Left (-1 via Negate)
        key_left = unreal.Key()
        key_left.import_text('Left')
        map_left = imc.map_key(ia_steer, key_left)
        neg_left = unreal.new_object(unreal.InputModifierNegate, outer=imc)
        map_left.set_editor_property('modifiers', [neg_left])

        # Gamepad Left Stick X
        key_gp_x = unreal.Key()
        key_gp_x.import_text('Gamepad_LeftX')
        imc.map_key(ia_steer, key_gp_x)

        # Map Sheet
        # Key W (+1)
        key_w = unreal.Key()
        key_w.import_text('W')
        imc.map_key(ia_sheet, key_w)

        # Key S (-1 via Negate)
        key_s = unreal.Key()
        key_s.import_text('S')
        map_s = imc.map_key(ia_sheet, key_s)
        neg_s = unreal.new_object(unreal.InputModifierNegate, outer=imc)
        map_s.set_editor_property('modifiers', [neg_s])

        # Key Up (+1)
        key_up = unreal.Key()
        key_up.import_text('Up')
        imc.map_key(ia_sheet, key_up)

        # Key Down (-1 via Negate)
        key_down = unreal.Key()
        key_down.import_text('Down')
        map_down = imc.map_key(ia_sheet, key_down)
        neg_down = unreal.new_object(unreal.InputModifierNegate, outer=imc)
        map_down.set_editor_property('modifiers', [neg_down])

        # Gamepad Right Trigger
        key_rt = unreal.Key()
        key_rt.import_text('Gamepad_RightTriggerAxis')
        imc.map_key(ia_sheet, key_rt)

        # Map Edge
        # Key E (+1)
        key_e = unreal.Key()
        key_e.import_text('E')
        imc.map_key(ia_edge, key_e)

        # Key Q (-1 via Negate)
        key_q = unreal.Key()
        key_q.import_text('Q')
        map_q = imc.map_key(ia_edge, key_q)
        neg_q = unreal.new_object(unreal.InputModifierNegate, outer=imc)
        map_q.set_editor_property('modifiers', [neg_q])

        # Gamepad Left Stick Y
        key_gp_y = unreal.Key()
        key_gp_y.import_text('Gamepad_LeftY')
        imc.map_key(ia_edge, key_gp_y)

        editor_asset_lib.save_loaded_asset(imc)
        editor_asset_lib.save_asset('/Game/Input/IMC_Default', False)
        print(f'IMC_Default configured and saved: {imc}')

        print('=== Creating BP_KiteRider ===')
        bf_rider = unreal.BlueprintFactory()
        bf_rider.set_editor_property('parent_class', unreal.KiteRiderPawn)
        bp_rider = get_or_create_asset('BP_KiteRider', '/Game/Blueprints', unreal.Blueprint, bf_rider)

        cdo_rider = unreal.get_default_object(bp_rider.generated_class())
        cdo_rider.set_editor_property('default_mapping_context', imc)
        cdo_rider.set_editor_property('steer_action', ia_steer)
        cdo_rider.set_editor_property('sheet_action', ia_sheet)

        boom = cdo_rider.get_editor_property('camera_boom')
        if boom:
            boom.set_editor_property('target_arm_length', 900.0)
            rot = boom.get_editor_property('relative_rotation')
            rot.pitch = -15.0
            boom.set_editor_property('relative_rotation', rot)
            boom.set_editor_property('enable_camera_lag', True)
            boom.set_editor_property('camera_lag_speed', 6.0)
            boom.set_editor_property('enable_camera_rotation_lag', True)
            boom.set_editor_property('camera_rotation_lag_speed', 6.0)

        cam = cdo_rider.get_editor_property('follow_camera')
        if cam:
            cam.set_editor_property('field_of_view', 80.0)

        editor_asset_lib.save_loaded_asset(bp_rider)
        editor_asset_lib.save_asset('/Game/Blueprints/BP_KiteRider', False)
        print(f'BP_KiteRider configured and saved: {bp_rider}')

        print('=== Creating BP_KiteSurfHUD ===')
        bf_hud = unreal.BlueprintFactory()
        bf_hud.set_editor_property('parent_class', unreal.KiteSurfHUD)
        bp_hud = get_or_create_asset('BP_KiteSurfHUD', '/Game/Blueprints', unreal.Blueprint, bf_hud)
        editor_asset_lib.save_loaded_asset(bp_hud)
        editor_asset_lib.save_asset('/Game/Blueprints/BP_KiteSurfHUD', False)
        print(f'BP_KiteSurfHUD configured and saved: {bp_hud}')

        print('=== Creating BP_KiteSurfGameMode ===')
        bf_gm = unreal.BlueprintFactory()
        bf_gm.set_editor_property('parent_class', unreal.KiteSurfGameMode)
        bp_gm = get_or_create_asset('BP_KiteSurfGameMode', '/Game/Blueprints', unreal.Blueprint, bf_gm)

        cdo_gm = unreal.get_default_object(bp_gm.generated_class())
        cdo_gm.set_editor_property('default_pawn_class', bp_rider.generated_class())
        cdo_gm.set_editor_property('hud_class', bp_hud.generated_class())
        editor_asset_lib.save_loaded_asset(bp_gm)
        editor_asset_lib.save_asset('/Game/Blueprints/BP_KiteSurfGameMode', False)
        print(f'BP_KiteSurfGameMode configured and saved: {bp_gm}')

        # Cleanup any legacy test assets
        for test_asset in ['/Game/Input/IA_Test', '/Game/Input/IMC_Test', '/Game/Blueprints/BP_TestRider', '/Game/Blueprints/BP_TestGM']:
            if editor_asset_lib.does_asset_exist(test_asset):
                editor_asset_lib.delete_asset(test_asset)
                print(f'Deleted test asset: {test_asset}')

        print('=== ALL ASSETS CREATED AND CONFIGURED SUCCESSFULLY ===')
    except Exception as e:
        print(f'ERROR: {e}\n{traceback.format_exc()}')
        sys.exit(1)

if __name__ == '__main__':
    build_assets()
