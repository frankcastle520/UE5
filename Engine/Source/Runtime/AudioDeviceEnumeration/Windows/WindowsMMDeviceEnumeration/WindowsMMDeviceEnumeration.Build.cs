// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class WindowsMMDeviceEnumeration : ModuleRules
{
	public WindowsMMDeviceEnumeration(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateIncludePathModuleNames.Add("TargetPlatform");

		if (Target.bCompileAgainstEngine)
        {
            PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					 "Engine",
				}
			);
		}

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
            }
		);

		PrecompileForTargets = PrecompileTargetsType.None;

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PrecompileForTargets = PrecompileTargetsType.Any;
		}
	}
}
