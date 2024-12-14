// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class TVOSTargetPlatformSettings : ModuleRules
{
	public TVOSTargetPlatformSettings(ReadOnlyTargetRules Target) : base(Target)
	{
		BinariesSubFolder = "IOS";
		// We need a short name here since this can run afoul very easily of the `MAX_PATH` limit when
		// combined with building other targets that make use of this.
		ShortName = "TVOSTPSet";

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"TargetPlatform",
				"DesktopPlatform",
				"IOSTargetPlatformSettings",
			}
		);

		if (Target.bCompileAgainstEngine)
		{
			PrivateDependencyModuleNames.Add("Engine");
		}

		PrivateIncludePaths.AddRange(
			new string[] {
			"Developer/IOS/IOSTargetPlatformSettings/Private"
			}
		);
	}
}
