import unreal

print("=== Creating Open-Water Level L_OpenWater ===")

map_path = '/Game/Maps/L_OpenWater'

# 1. Create or load the level
level_editor_subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)

try:
    # Try creating new level
    success = unreal.EditorLevelLibrary.new_level(map_path)
    if not success:
        print(f"new_level returned False, loading {map_path}...")
        unreal.EditorLevelLibrary.load_level(map_path)
except Exception as e:
    print(f"Error creating level: {e}, attempting load...")
    unreal.EditorLevelLibrary.load_level(map_path)

# Clear any existing actors if we loaded an existing level to ensure clean state
existing_actors = unreal.EditorLevelLibrary.get_all_level_actors()
for actor in existing_actors:
    if actor.get_class().get_name() not in ['WorldSettings']:
        unreal.EditorLevelLibrary.destroy_actor(actor)

# 2. Spawn Directional Light (Sun)
sun_actor = unreal.EditorLevelLibrary.spawn_actor_from_class(
    unreal.DirectionalLight,
    unreal.Vector(0, 0, 1000),
    unreal.Rotator(-45, -30, 0)
)
if sun_actor:
    sun_actor.set_actor_label('DirectionalLight_Sun')
    light_comp = sun_actor.get_component_by_class(unreal.DirectionalLightComponent)
    if light_comp:
        light_comp.set_editor_property('atmosphere_sun_light', True)
        light_comp.set_editor_property('intensity', 10.0)
    print("Spawned and configured DirectionalLight")

# 3. Spawn SkyAtmosphere
sky_atm = unreal.EditorLevelLibrary.spawn_actor_from_class(
    unreal.SkyAtmosphere,
    unreal.Vector(0, 0, 0)
)
if sky_atm:
    sky_atm.set_actor_label('SkyAtmosphere')
    print("Spawned SkyAtmosphere")

# 4. Spawn SkyLight
skylight = unreal.EditorLevelLibrary.spawn_actor_from_class(
    unreal.SkyLight,
    unreal.Vector(0, 0, 500)
)
if skylight:
    skylight.set_actor_label('SkyLight')
    sl_comp = skylight.get_component_by_class(unreal.SkyLightComponent)
    if sl_comp:
        sl_comp.set_editor_property('real_time_capture', True)
    print("Spawned and configured SkyLight")

# 5. Spawn ExponentialHeightFog
fog = unreal.EditorLevelLibrary.spawn_actor_from_class(
    unreal.ExponentialHeightFog,
    unreal.Vector(0, 0, 0)
)
if fog:
    fog.set_actor_label('ExponentialHeightFog')
    fog_comp = fog.get_component_by_class(unreal.ExponentialHeightFogComponent)
    if fog_comp:
        try:
            fog_comp.set_editor_property('enable_volumetric_fog', True)
        except Exception as e:
            print(f"Warning: Failed to enable volumetric fog: {e}")
    print("Spawned and configured ExponentialHeightFog")

# 6. Spawn VolumetricCloud
cloud = unreal.EditorLevelLibrary.spawn_actor_from_class(
    unreal.VolumetricCloud,
    unreal.Vector(0, 0, 0)
)
if cloud:
    cloud.set_actor_label('VolumetricCloud')
    print("Spawned VolumetricCloud")

# 7. Spawn WaterZone if required by Water plugin, or WaterBodyOcean
# In UE 5.x, WaterBodyOcean requires or works with a WaterZone in the level
if hasattr(unreal, 'WaterZone'):
    water_zone = unreal.EditorLevelLibrary.spawn_actor_from_class(
        unreal.WaterZone,
        unreal.Vector(0, 0, 0)
    )
    if water_zone:
        water_zone.set_actor_label('WaterZone')
        print("Spawned WaterZone")

# Spawn WaterBodyOcean at world Z=0
ocean = unreal.EditorLevelLibrary.spawn_actor_from_class(
    unreal.WaterBodyOcean,
    unreal.Vector(0, 0, 0)
)
if ocean:
    ocean.set_actor_label('WaterBodyOcean')
    print("Spawned WaterBodyOcean at Z=0")

# 8. Spawn PlayerStart at roughly (0, 0, 50)
player_start = unreal.EditorLevelLibrary.spawn_actor_from_class(
    unreal.PlayerStart,
    unreal.Vector(0, 0, 50)
)
if player_start:
    player_start.set_actor_label('PlayerStart')
    print("Spawned PlayerStart at (0, 0, 50)")

# 9. Save current level
saved = unreal.EditorLevelLibrary.save_current_level()
print(f"save_current_level returned: {saved}")

# Also save all dirty packages via EditorAssetLibrary to be 100% sure
unreal.EditorAssetLibrary.save_loaded_asset(unreal.EditorAssetLibrary.load_asset(map_path), only_if_is_dirty=False)

all_actors = unreal.EditorLevelLibrary.get_all_level_actors()
print(f"Total level actors: {len(all_actors)}")
for a in all_actors:
    print(f" - {a.get_name()} ({a.get_class().get_name()}) at {a.get_actor_location()}")

print("=== Open-Water Level L_OpenWater Created Successfully ===")
