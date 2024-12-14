// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class InstancedActorsEditor : ModuleRules
	{
		public InstancedActorsEditor(ReadOnlyTargetRules Target) : base(Target)
		{
			PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

			PublicIncludePaths.AddRange(
				new string[] {
				}
			);

			PublicDependencyModuleNames.AddRange(
				new string[] {
					"Core",
					"CoreUObject",
					"Engine",
					"UnrealEd",
					"LevelEditor",
					"SlateCore",
					"Slate",
					"LevelEditor",
					"InstancedActors",
					"MassEntity"
				}
			);
		}
	}
}
