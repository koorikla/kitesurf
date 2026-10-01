using UnrealBuildTool;
using System.Collections.Generic;

public class KiteSurfEditorTarget : TargetRules
{
	public KiteSurfEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V5;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("KiteSurf");
	}
}
