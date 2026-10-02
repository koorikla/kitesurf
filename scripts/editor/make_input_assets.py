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

        # 4. IA_Jump (Digital bool)
        ia_jump = get_or_create_asset('IA_Jump', '/Game/Input', unreal.InputAction, None)
        ia_jump.set_editor_property('value_type', unreal.InputActionValueType.BOOLEAN)
        editor_asset_lib.save_loaded_asset(ia_jump)
        editor_asset_lib.save_asset('/Game/Input/IA_Jump', False)
        print(f'IA_Jump configured and saved: {ia_jump}')

        # 5. IA_Pause (Digital bool)
        ia_pause = get_or_create_asset('IA_Pause', '/Game/Input', unreal.InputAction, None)
        ia_pause.set_editor_property('value_type', unreal.InputActionValueType.BOOLEAN)
        editor_asset_lib.save_loaded_asset(ia_pause)
        editor_asset_lib.save_asset('/Game/Input/IA_Pause', False)
        print(f'IA_Pause configured and saved: {ia_pause}')

        # 6. IA_Reset (Digital bool)
        ia_reset = get_or_create_asset('IA_Reset', '/Game/Input', unreal.InputAction, None)
        ia_reset.set_editor_property('value_type', unreal.InputActionValueType.BOOLEAN)
        editor_asset_lib.save_loaded_asset(ia_reset)
        editor_asset_lib.save_asset('/Game/Input/IA_Reset', False)
        print(f'IA_Reset configured and saved: {ia_reset}')

        print('=== Creating Input Mapping Context ===')
        imc = get_or_create_asset('IMC_Default', '/Game/Input', unreal.InputMappingContext, None)
        imc.unmap_all()

        mappings_spec = [
            # Steer
            (ia_steer, 'D', False),
            (ia_steer, 'A', True),
            (ia_steer, 'Right', False),
            (ia_steer, 'Left', True),
            (ia_steer, 'Gamepad_LeftX', False),
            # Sheet
            (ia_sheet, 'W', False),
            (ia_sheet, 'S', True),
            (ia_sheet, 'Up', False),
            (ia_sheet, 'Down', True),
            (ia_sheet, 'Gamepad_RightTriggerAxis', False),
            # Edge
            (ia_edge, 'E', False),
            (ia_edge, 'Q', True),
            (ia_edge, 'Gamepad_LeftY', False),
            # Jump
            (ia_jump, 'SpaceBar', False),
            (ia_jump, 'Gamepad_FaceButton_Bottom', False),
            # Pause
            (ia_pause, 'Escape', False),
            (ia_pause, 'Gamepad_Special_Right', False),
            # Reset (R and Gamepad Face Button Right)
            (ia_reset, 'R', False),
            (ia_reset, 'Gamepad_FaceButton_Right', False),
        ]

        for action, key_str, _ in mappings_spec:
            k = unreal.Key()
            k.import_text(key_str)
            imc.map_key(action, k)

        dkm = imc.get_editor_property('default_key_mappings')
        mappings = list(dkm.get_editor_property('mappings'))
        for i, (action, key_str, negate) in enumerate(mappings_spec):
            if negate:
                neg = unreal.new_object(unreal.InputModifierNegate, outer=imc)
                mappings[i].set_editor_property('modifiers', [neg])

        dkm.set_editor_property('mappings', mappings)
        imc.set_editor_property('default_key_mappings', dkm)

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
        cdo_rider.set_editor_property('edge_action', ia_edge)
        cdo_rider.set_editor_property('jump_action', ia_jump)
        cdo_rider.set_editor_property('pause_action', ia_pause)
        if hasattr(cdo_rider, 'reset_action'):
            cdo_rider.set_editor_property('reset_action', ia_reset)

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

        rider_comp = cdo_rider.get_editor_property('rider_mesh')
        anim_asset = editor_asset_lib.load_asset('/Game/Characters/Mannequins/Anims/MM_Idle')
        if rider_comp and anim_asset:
            try:
                rider_comp.set_editor_property('animation_mode', unreal.AnimationMode.ANIMATION_SINGLE_NODE)
                anim_data = rider_comp.get_editor_property('animation_data')
                anim_data.set_editor_property('anim_to_play', anim_asset)
                anim_data.set_editor_property('saved_playing', True)
                anim_data.set_editor_property('saved_looping', True)
                rider_comp.set_editor_property('animation_data', anim_data)
                print('Set MM_Idle animation on rider_mesh')
            except Exception as e:
                print(f'Note on rider animation setting: {e}')

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
