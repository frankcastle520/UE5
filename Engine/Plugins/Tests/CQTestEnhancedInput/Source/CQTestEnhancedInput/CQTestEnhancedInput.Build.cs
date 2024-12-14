// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class CQTestEnhancedInput : ModuleRules
{
	public CQTestEnhancedInput(ReadOnlyTargetRules Target)
		: base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(
			new string[] {
					"Core",
					"CoreUObject",
					"Engine",
					"EnhancedInput"
				 }
			);
	}
}
