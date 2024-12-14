// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class MassEntityEditor : ModuleRules
	{
		public MassEntityEditor(ReadOnlyTargetRules Target) : base(Target)
		{
			PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

			UnsafeTypeCastWarningLevel = WarningLevel.Warning;

			PublicDependencyModuleNames.AddRange(
				new string[] {
					"Core",
					"CoreUObject",
					"Engine",
					"InputCore",
					"AssetTools",
					"UnrealEd",
					"Slate",
					"SlateCore",
					"PropertyEditor",
					"MassEntity",
					"DetailCustomizations",
					"ComponentVisualizers",
					"Projects",
					"EditorSubsystem",
				}
			);

			PrivateDependencyModuleNames.AddRange(
				new string[] {
					"RenderCore",
					"GraphEditor",
					"KismetWidgets",
					"AIGraph",
					"ToolMenus",
				}
			);

			if (Target.bBuildDeveloperTools == true)
			{
				PrivateDependencyModuleNames.Add("MessageLog");
				DynamicallyLoadedModuleNames.Add("MassEntityDebugger");
			}
		}
	}
}
