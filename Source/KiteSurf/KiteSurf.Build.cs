using UnrealBuildTool;

public class KiteSurf : ModuleRules
{
	public KiteSurf(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"PhysicsCore",
			"Water",
			"CableComponent",
			"UMG",
			"Slate",
			"SlateCore",
			"EngineSettings"
		});

		PrivateDependencyModuleNames.AddRange(new string[] {
			"AudioMixer",
			"MetasoundEngine"
		});
	}
}
