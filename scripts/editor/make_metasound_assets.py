import unreal

def create_or_overwrite_metasound(name, input_floats):
    builder_sub = unreal.get_engine_subsystem(unreal.MetaSoundBuilderSubsystem)
    ed_sub = unreal.get_editor_subsystem(unreal.MetaSoundEditorSubsystem)
    el = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)

    asset_path = f'/Game/Audio/{name}'
    if el.does_asset_exist(asset_path):
        unreal.log(f'Asset {asset_path} already exists, skipping creation.')
        return

    builder_name = f'{name}_Builder'
    res = builder_sub.create_source_builder(
        builder_name,
        unreal.MetaSoundOutputAudioFormat.STEREO,
        False
    )
    builder = [x for x in res if isinstance(x, unreal.MetaSoundSourceBuilder)][0]
    unreal.log(f'Created builder for {name}: {builder}')

    for param_name in input_floats:
        literal = unreal.MetasoundFrontendLiteral()
        literal.import_text('(Type=Float,AsFloat=(0.000000))')
        out_handle = builder.add_graph_input_node(param_name, 'Float', literal)
        unreal.log(f'Added input {param_name} to {name}: {out_handle}')

    asset_res = ed_sub.build_to_asset(builder, 'KiteSurf', name, '/Game/Audio')
    unreal.log(f'build_to_asset result for {name}: {asset_res}')
    el.save_asset(asset_path, False)
    unreal.log(f'Saved {asset_path}')

# Continuous Bed (ApparentWind, LineTension, BoardSpeed)
create_or_overwrite_metasound('MS_AudioBed', ['ApparentWind', 'LineTension', 'BoardSpeed'])

# One-shots
create_or_overwrite_metasound('MS_Crash', ['CrashIntensity'])
create_or_overwrite_metasound('MS_Pop', [])
create_or_overwrite_metasound('MS_Landing', ['LandingG'])
create_or_overwrite_metasound('MS_ResetCue', [])

print('Successfully ensured all MetaSound assets exist!')
