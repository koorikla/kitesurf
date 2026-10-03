import unreal

# Run with a renderer, or the ocean is saved without a mesh and is invisible in game:
#   scripts/run-python.sh scripts/editor/make_open_water_level.py -AllowCommandletRendering -vulkan -RenderOffScreen
# The Water plugin skips building water body render data when FApp::CanEverRender() is
# false (UWaterBodyComponent::UpdateWaterBodyRenderData), which is the case for a plain
# pythonscript commandlet. The script checks for this before saving.

print("=== Creating Open-Water Level L_FlatWater ===")

map_path = '/Game/Maps/L_FlatWater'

# Sun elevation 40 deg, shining from behind-left of the downwind (+X) spawn heading.
SUN_PITCH_DEG = -40.0
SUN_YAW_DEG = -30.0

# The ocean is 100 km square, so a session cannot reach its edge (about an hour in a straight
# line at 27 kn from the centre).
ZONE_EXTENT_CM = 10000000.0
# A zone that large cannot be one static quadtree (capped at 256 tiles per side by
# r.Water.WaterMesh.MaxDimensionInTiles), so the wave-displaced mesh is built in a window that
# follows the camera: 256 tiles of 24 m, 6.1 km across. Outside it the ocean's own flat static
# mesh carries the water to the horizon.
WATER_TILE_SIZE_CM = 2400.0
LOCAL_TESSELLATION_EXTENT_CM = 256 * WATER_TILE_SIZE_CM
# An ocean body cuts a hole for its "island": the bounding box of its spline plus its own
# origin (WaterBodyOceanComponent.cpp, GenerateWaterBodyMesh). The default island is a
# 200 m square around the actor, so park the actor near the zone corner, 70 km from spawn.
OCEAN_ISLAND_OFFSET_CM = -(ZONE_EXTENT_CM / 2.0 - 50000.0)

# Light sea haze; the engine default (0.02) hides the horizon and the water surface.
FOG_DENSITY = 0.004

# 1. Create or load the level
level_editor_subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)

if unreal.EditorAssetLibrary.does_asset_exist(map_path):
    print(f"Level exists, loading {map_path}...")
    level_editor_subsystem.load_level(map_path)
else:
    print(f"Creating new level at {map_path}...")
    level_editor_subsystem.new_level(map_path)

# Clear any existing actors if we loaded an existing level to ensure clean state
existing_actors = unreal.EditorLevelLibrary.get_all_level_actors()
for actor in existing_actors:
    if actor.get_class().get_name() not in ['WorldSettings']:
        unreal.EditorLevelLibrary.destroy_actor(actor)

# 2. Spawn Directional Light (Sun)
sun_actor = unreal.EditorLevelLibrary.spawn_actor_from_class(
    unreal.DirectionalLight,
    unreal.Vector(0, 0, 1000),
    # Positional Rotator args are (roll, pitch, yaw), so name them.
    unreal.Rotator(roll=0.0, pitch=SUN_PITCH_DEG, yaw=SUN_YAW_DEG)
)
if sun_actor:
    sun_actor.set_actor_label('DirectionalLight_Sun')
    light_comp = sun_actor.get_component_by_class(unreal.DirectionalLightComponent)
    if light_comp:
        light_comp.set_editor_property('mobility', unreal.ComponentMobility.MOVABLE)
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
        sl_comp.set_editor_property('mobility', unreal.ComponentMobility.MOVABLE)
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
        fog_comp.set_editor_property('fog_density', FOG_DENSITY)
        # Volumetric fog adds a dense near-field haze over open water and costs GPU time.
        fog_comp.set_editor_property('enable_volumetric_fog', False)
    print("Spawned and configured ExponentialHeightFog")

# 6. Spawn VolumetricCloud
cloud = unreal.EditorLevelLibrary.spawn_actor_from_class(
    unreal.VolumetricCloud,
    unreal.Vector(0, 0, 0)
)
if cloud:
    cloud.set_actor_label('VolumetricCloud')
    print("Spawned VolumetricCloud")

# 7. Spawn the WaterZone first so the ocean fills it when it is created
water_zone = unreal.EditorLevelLibrary.spawn_actor_from_class(
    unreal.WaterZone,
    unreal.Vector(0, 0, 0)
)
if not water_zone:
    raise RuntimeError("Failed to spawn WaterZone")
water_zone.set_actor_label('WaterZone')
water_zone.set_editor_property('zone_extent', unreal.Vector2D(ZONE_EXTENT_CM, ZONE_EXTENT_CM))
water_zone.set_editor_property('enable_local_only_tessellation', True)
water_zone.set_editor_property(
    'local_tessellation_extent',
    unreal.Vector(LOCAL_TESSELLATION_EXTENT_CM, LOCAL_TESSELLATION_EXTENT_CM, 10000.0)
)
for c in water_zone.get_components_by_class(unreal.WaterMeshComponent):
    far_mat = unreal.EditorAssetLibrary.load_asset('/Water/Materials/WaterSurface/Water_FarMesh')
    if far_mat:
        c.set_editor_property('far_distance_material', far_mat)
        c.set_editor_property('far_distance_mesh_extent', ZONE_EXTENT_CM)
        print("Configured WaterMeshComponent far distance mesh")
    c.set_editor_property('tile_size', WATER_TILE_SIZE_CM)
    break
print(f"Spawned WaterZone with extent {ZONE_EXTENT_CM} cm")

# Spawn WaterBodyOcean at world Z=0, with its island hole near the zone corner
ocean = unreal.EditorLevelLibrary.spawn_actor_from_class(
    unreal.WaterBodyOcean,
    unreal.Vector(OCEAN_ISLAND_OFFSET_CM, OCEAN_ISLAND_OFFSET_CM, 0)
)
if not ocean:
    raise RuntimeError("Failed to spawn WaterBodyOcean")
ocean.set_actor_label('WaterBodyOcean')
print("Spawned WaterBodyOcean at Z=0")

# Configure Gerstner Waves on ocean
print("Setting up Gerstner Water Waves...")
waves = unreal.new_object(unreal.GerstnerWaterWaves, ocean)
gen = unreal.new_object(unreal.GerstnerWaterWaveGeneratorSimple, waves)

# Wind chop for ~18 kn: crest-to-trough up to about a metre. Each wave shifts the surface
# sideways by steepness / wavenumber regardless of its amplitude (FGerstnerWave::Recompute),
# so the surface folds over itself into foam-white lumps once the steepness values sum past
# 1. Keep num_waves * steepness well below that.
gen.set_editor_property('num_waves', 1)
gen.set_editor_property('min_wavelength', 800.0)
gen.set_editor_property('max_wavelength', 4000.0)
gen.set_editor_property('min_amplitude', 0.1)
gen.set_editor_property('max_amplitude', 1.0)
gen.set_editor_property('wind_angle_deg', 0.0)
gen.set_editor_property('direction_angular_spread_deg', 40.0)
gen.set_editor_property('small_wave_steepness', 0.01)
gen.set_editor_property('large_wave_steepness', 0.01)

# Assign the generator last: UGerstnerWaterWaves caches the generated wave list and only
# recomputes it when one of its own properties is set, not when the generator is edited.
waves.set_editor_property('gerstner_wave_generator', gen)

ocean.set_editor_property('water_waves', waves)
print("Configured Gerstner waves on ocean")

# The ocean sizes itself to the zone and builds its mesh when it is spawned
# (UWaterBodyOceanComponent::OnPostActorCreated), which is why the zone is spawned first.
ocean_comp = ocean.get_water_body_component()
ocean_extents = ocean_comp.get_editor_property('ocean_extents')
if abs(ocean_extents.x - ZONE_EXTENT_CM) > 1.0 or abs(ocean_extents.y - ZONE_EXTENT_CM) > 1.0:
    raise RuntimeError(f"Ocean did not fill the water zone: extents {ocean_extents}")

# The fallback mesh for everything outside the tessellated window.
ocean_comp.set_water_body_static_mesh_enabled(True)

# Refuse to save an ocean with no mesh (see the note at the top of this file).
info_meshes = ocean.get_components_by_class(unreal.WaterBodyInfoMeshComponent)
if not info_meshes or any(c.get_editor_property('static_mesh') is None for c in info_meshes):
    raise RuntimeError(
        "Ocean render data was not built. Re-run with: "
        "-AllowCommandletRendering -vulkan -RenderOffScreen"
    )
print(f"Ocean fills the {ZONE_EXTENT_CM} cm zone; island parked at ({OCEAN_ISLAND_OFFSET_CM}, {OCEAN_ISLAND_OFFSET_CM})")

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

print("=== Open-Water Level L_FlatWater Created Successfully ===")
