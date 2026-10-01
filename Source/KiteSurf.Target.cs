using UnrealBuildTool;
using System.Collections.Generic;

public class KiteSurfTarget : TargetRules
{
	public KiteSurfTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V5;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("KiteSurf");
	}
}
