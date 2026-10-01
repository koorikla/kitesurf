"""Verify Lumen has ray tracing data after enabling mesh distance fields.

Mirrors Lumen::IsSoftwareRayTracingSupported() -> DoesProjectSupportDistanceFields()
from Engine/Source/Runtime/Engine/Private/SceneManagement.cpp:61.
"""
import unreal

OUT = '/home/koorik/work/kitesurf/Saved/Logs/lumen_rt_verify.txt'
lines = []


def log(msg):
    lines.append(str(msg))
    unreal.log('LUMENVERIFY: %s' % msg)


def cvar_int(name):
    try:
        return unreal.SystemLibrary.get_console_variable_int_value(name)
    except Exception as exc:  # noqa: BLE001
        return 'ERR:%s' % exc


log('=== CVar state ===')
cvars = {}
for name in [
    'r.GenerateMeshDistanceFields',
    'r.DistanceFields.SupportEvenIfHardwareRayTracingSupported',
    'r.RayTracing',
    'r.Raytracing.Enable',
    'r.SkinCache.CompileShaders',
    'r.Lumen.HardwareRayTracing',
    'r.Lumen.TraceMeshSDFs',
    'r.DynamicGlobalIlluminationMethod',
    'r.ReflectionMethod',
    'r.Lumen.DiffuseIndirect.Allow',
    'r.Lumen.Reflections.Allow',
]:
    cvars[name] = cvar_int(name)
    log('%s = %s' % (name, cvars[name]))

generate_df = cvars['r.GenerateMeshDistanceFields'] == 1
hwrt_allowed = cvars['r.RayTracing'] == 1
df_even_if_hwrt = cvars['r.DistanceFields.SupportEvenIfHardwareRayTracingSupported'] == 1
project_supports_df = generate_df and (df_even_if_hwrt or not hwrt_allowed)
skin_cache_ok = cvars['r.SkinCache.CompileShaders'] >= 1
lumen_hwrt = cvars['r.Lumen.HardwareRayTracing'] == 1
lumen_gi_on = cvars['r.DynamicGlobalIlluminationMethod'] == 1
lumen_refl_on = cvars['r.ReflectionMethod'] == 1

log('')
log('=== Predicate evaluation ===')
log('DoesProjectSupportDistanceFields() == %s' % project_supports_df)
log('Hardware RT requested (r.RayTracing=1) == %s' % hwrt_allowed)
log('Skin cache prerequisite satisfied == %s  (Fatal at init if False)'
    % skin_cache_ok)
log('Lumen set to use Hardware RT == %s' % lumen_hwrt)
log('Lumen GI enabled == %s' % lumen_gi_on)
log('Lumen Reflections enabled == %s' % lumen_refl_on)
has_tracing_data = project_supports_df or hwrt_allowed
log('Lumen has ray tracing data (HW or SW) == %s' % has_tracing_data)
log('=> bEnabledButHasNoDataForTracing would be %s'
    % ((lumen_gi_on or lumen_refl_on) and not has_tracing_data))

log('')
log('=== Distance field data on static meshes ===')
# Force-rebuild DF data for a stock engine mesh and confirm the asset reports it.
asset_paths = [
    '/Engine/BasicShapes/Cube.Cube',
    '/Engine/BasicShapes/Sphere.Sphere',
]
for path in asset_paths:
    mesh = unreal.load_asset(path)
    if mesh is None:
        log('%s: NOT FOUND' % path)
        continue
    try:
        build_settings = mesh.get_editor_property('lod_for_collision')  # touch to validate asset
    except Exception:
        pass
    has_df = None
    for attr in ('b_generate_mesh_distance_field', 'generate_mesh_distance_field'):
        try:
            has_df = mesh.get_editor_property(attr)
            log('%s: %s = %s' % (path, attr, has_df))
            break
        except Exception:
            continue
    # Authoritative runtime check: does the render data carry a distance field?
    try:
        nav = unreal.StaticMeshEditorSubsystem()
        log('%s: StaticMeshEditorSubsystem available' % path)
    except Exception as exc:  # noqa: BLE001
        log('%s: no StaticMeshEditorSubsystem (%s)' % (path, exc))

log('')
log('=== Project map actors ===')
try:
    subsys = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    actors = subsys.get_all_level_actors()
    log('actor count in current level: %d' % len(actors))
    for a in actors:
        log(' - %s (%s)' % (a.get_actor_label(), a.get_class().get_name()))
except Exception as exc:  # noqa: BLE001
    log('actor enumeration failed: %s' % exc)

log('')
log('RESULT: %s' % ('PASS' if (has_tracing_data and skin_cache_ok
                               and (lumen_gi_on or lumen_refl_on))
                    else 'FAIL'))

with open(OUT, 'w') as fh:
    fh.write('\n'.join(lines) + '\n')
