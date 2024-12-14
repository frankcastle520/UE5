// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class WindowsTargetPlatform : ModuleRules
{
	public WindowsTargetPlatform(ReadOnlyTargetRules Target) : base(Target)
	{
		SDKVersionRelevantPlatforms.Add(UnrealTargetPlatform.Win64);
		
		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"TargetPlatform",
				"DesktopPlatform",
				"WindowsTargetPlatformSettings",
				"WindowsTargetPlatformControls",
			}
		);
    }
}
