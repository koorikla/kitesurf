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

		// The motion bar reads controller gyro and accelerometer through the SDL instance the
		// engine already runs (ApplicationCore exports it). Headers only: linking the static
		// library here would make a second, uninitialised copy of SDL.
		if (Target.IsInPlatformGroup(UnrealPlatformGroup.Unix))
		{
			PrivateDependencyModuleNames.Add("ApplicationCore");
			PrivateIncludePaths.Add(System.IO.Path.Combine(EngineDirectory, "Source", "ThirdParty", "SDL3", "SDL-gui-backend", "include"));
			PrivateDefinitions.Add("SDL_WITH_EPIC_EXTENSIONS=1");
		}
	}
}
