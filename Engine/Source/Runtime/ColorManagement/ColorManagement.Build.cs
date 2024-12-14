// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using UnrealBuildTool;

public class ColorManagement : ModuleRules
{
	public ColorManagement(ReadOnlyTargetRules Target) : base(Target)
	{
		// Note: ColorManagement module is now deprecated in 5.5, as it was moved into Core.

		PublicDependencyModuleNames.AddRange(
			new string[] {
				"Core",
			}
		);
	}
}
