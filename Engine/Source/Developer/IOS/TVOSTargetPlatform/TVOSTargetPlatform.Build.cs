// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class TVOSTargetPlatform : ModuleRules
{
	public TVOSTargetPlatform(ReadOnlyTargetRules Target) : base(Target)
	{
		BinariesSubFolder = "IOS";
		SDKVersionRelevantPlatforms.Add(UnrealTargetPlatform.TVOS);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"TargetPlatform",
				"DesktopPlatform",
			}
		);
	}
}
