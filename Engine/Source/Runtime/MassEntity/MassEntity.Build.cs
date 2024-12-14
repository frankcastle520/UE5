// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class MassEntity : ModuleRules
{
	public MassEntity(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		UnsafeTypeCastWarningLevel = WarningLevel.Warning;

		PublicDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"Engine",
				"DeveloperSettings",
			}
		);

		if (Target.bBuildEditor || Target.bCompileAgainstEditor)
		{
			PrivateDependencyModuleNames.Add("UnrealEd");
			PrivateDependencyModuleNames.Add("EditorSubsystem");
		}

		if (Target.Configuration != UnrealTargetConfiguration.Shipping
			&& Target.Configuration != UnrealTargetConfiguration.Test)
		{
			// pulling this one in for the testableEnsureMsgf
			PrivateDependencyModuleNames.Add("AITestSuite");
		}

		if (Target.bBuildDeveloperTools)
		{
			DynamicallyLoadedModuleNames.Add("MassEntityTestSuite");
		}
	}
}
