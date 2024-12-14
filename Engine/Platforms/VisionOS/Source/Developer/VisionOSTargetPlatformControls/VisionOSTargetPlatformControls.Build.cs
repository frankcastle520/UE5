// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class VisionOSTargetPlatformControls : ModuleRules
{
	public VisionOSTargetPlatformControls(ReadOnlyTargetRules Target) : base(Target)
	{
		BinariesSubFolder = "IOS";
		// We need a short name here since this can run afoul very easily of the `MAX_PATH` limit when
		// combined with building other targets that make use of this.
		ShortName = "VisionOSTPCon";
		SDKVersionRelevantPlatforms.Add(UnrealTargetPlatform.VisionOS);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"TargetPlatform",
				"DesktopPlatform",
				"LaunchDaemonMessages",
				"IOSTargetPlatformControls",
				"Projects"
			}
		);

		PrivateIncludePathModuleNames.AddRange(
			new string[] {
			"Messaging",
			"TargetDeviceServices",
		}
		);

		if (Target.bCompileAgainstEngine)
		{
			PrivateDependencyModuleNames.Add("Engine");
		}

		PrivateIncludePaths.AddRange(
			new string[] {
			"Developer/IOS/IOSTargetPlatformControls/Private"
			}
		);
	}
}
