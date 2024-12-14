// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class MathCore : ModuleRules
{
	public MathCore(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[] {
				"Core"
			}
		);

		UnsafeTypeCastWarningLevel = WarningLevel.Error;
	}
}
