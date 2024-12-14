// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class CQTestEnhancedInputTests : ModuleRules
{
	public CQTestEnhancedInputTests(ReadOnlyTargetRules Target)
		: base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(
			new string[] {
					"Core",
					"CoreUObject",
					"Engine",
					"InputCore",
					"EnhancedInput",
					"CQTest",
					"CQTestEnhancedInput",
				 }
			);
	}
}
