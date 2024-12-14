// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class AvalancheDisplayCluster : ModuleRules
{
	public AvalancheDisplayCluster(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core"
			});
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"AvalancheMedia",
				"DisplayCluster",
				"Engine"
			}
		);
	}
}
