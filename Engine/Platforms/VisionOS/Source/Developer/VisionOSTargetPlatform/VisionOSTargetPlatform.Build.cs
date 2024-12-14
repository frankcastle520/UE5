// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class VisionOSTargetPlatform : ModuleRules
{
	public VisionOSTargetPlatform(ReadOnlyTargetRules Target) : base(Target)
	{
		BinariesSubFolder = "IOS";
		SDKVersionRelevantPlatforms.Add(UnrealTargetPlatform.VisionOS);

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
