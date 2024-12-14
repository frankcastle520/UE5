// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class TedsCore : ModuleRules
{
	public TedsCore(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		if (Target.bBuildEditor)
		{
			PublicIncludePaths.AddRange(new string[] {});
			PrivateIncludePaths.AddRange(new string[] {});

			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"Engine",
					"EditorSubsystem",
					"MassEntity",
					"MassEntityEditor",
					"TypedElementFramework",
					"SlateCore",
					"UnrealEd"
				});

			PrivateDependencyModuleNames.AddRange(new string[] {});
			DynamicallyLoadedModuleNames.AddRange(new string[] {});
		}
	}
}
