#!/usr/bin/env python3
"""
make_main_menu_level.py
Creates Content/Maps/L_MainMenu.umap configured with AKiteSurfMainMenuGameMode
and an atmospheric backdrop for the menu UI.
"""
import unreal
import sys

def create_main_menu_level():
    subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    package_path = "/Game/Maps"
    map_name = "L_MainMenu"

    # Create empty world
    world = subsystem.new_level(f"{package_path}/{map_name}")
    if not world:
        print(f"ERROR: Failed to create level at {package_path}/{map_name}")
        return False

    editor_actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

    # Directional Light
    sun_actor = editor_actor_subsystem.spawn_actor_from_class(
        unreal.DirectionalLight,
        unreal.Vector(0, 0, 1000),
        unreal.Rotator(-20.0, 160.0, 0.0)
    )
    if sun_actor and sun_actor.root_component:
        sun_actor.root_component.set_mobility(unreal.ComponentMobility.MOVABLE)
        sun_actor.root_component.set_editor_property("intensity", 50000.0)
        sun_actor.root_component.set_editor_property("atmosphere_sun_light", True)

    # Sky Atmosphere & Light
    editor_actor_subsystem.spawn_actor_from_class(unreal.SkyAtmosphere, unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0))
    sky_light = editor_actor_subsystem.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0, 0, 500), unreal.Rotator(0, 0, 0))
    if sky_light and sky_light.light_component:
        sky_light.light_component.set_mobility(unreal.ComponentMobility.MOVABLE)
        sky_light.light_component.set_editor_property("real_time_capture", True)

    # Post Process
    ppv = editor_actor_subsystem.spawn_actor_from_class(unreal.PostProcessVolume, unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0))
    if ppv:
        ppv.set_editor_property("unbound", True)
        pp_settings = ppv.settings
        pp_settings.set_editor_property("dynamic_global_illumination_method", unreal.DynamicGlobalIlluminationMethod.LUMEN)
        pp_settings.set_editor_property("reflection_method", unreal.ReflectionMethod.LUMEN)

    # Set WorldSettings DefaultGameMode override to KiteSurfMainMenuGameMode
    world_settings = subsystem.get_current_level().get_world_settings()
    gm_class = unreal.load_class(None, "/Script/KiteSurf.KiteSurfMainMenuGameMode")
    if gm_class:
        world_settings.set_editor_property("default_game_mode", gm_class)
        print("Configured WorldSettings DefaultGameMode -> KiteSurfMainMenuGameMode")
    else:
        print("WARNING: Could not load /Script/KiteSurf.KiteSurfMainMenuGameMode")

    saved = subsystem.save_current_level()
    print(f"Level saved: {saved}")
    return saved

if __name__ == "__main__":
    success = create_main_menu_level()
    if not success:
        sys.exit(1)
    print("SUCCESS: L_MainMenu created")
    sys.exit(0)
