// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class MassEntityDebugger : ModuleRules
	{
		public MassEntityDebugger(ReadOnlyTargetRules Target) : base(Target)
		{
			PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

			PublicDependencyModuleNames.AddRange(
				new string[] {
					"Core",
					"CoreUObject",
					"Engine",
					"InputCore",
					"MassEntity",
					"SlateCore",
					"Slate",
					"WorkspaceMenuStructure",
					"Projects",
					"UMG"
				}
			);

			if (Target.bBuildEditor == true)
			{
				PublicDependencyModuleNames.AddRange(
					new string[] {
						"UnrealEd"
					}
				);
			}
		}
	}
}