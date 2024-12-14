// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class LinuxTargetPlatform : ModuleRules
{
    public LinuxTargetPlatform(ReadOnlyTargetRules Target) : base(Target)
	{
        BinariesSubFolder = "Linux";
		SDKVersionRelevantPlatforms.AddRange(Utils.GetPlatformsInGroup(UnrealPlatformGroup.Linux));

		PrivateDependencyModuleNames.AddRange(
            new string[] {
				"Core",
				"TargetPlatform",
				"DesktopPlatform",
				"LinuxTargetPlatformSettings",
				"LinuxTargetPlatformControls",
			}
        );
	}
}
