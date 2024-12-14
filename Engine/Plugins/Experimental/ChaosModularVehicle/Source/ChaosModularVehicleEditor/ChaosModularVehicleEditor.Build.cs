// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class ChaosModularVehicleEditor : ModuleRules
	{
		public ChaosModularVehicleEditor(ReadOnlyTargetRules Target) : base(Target)
		{
			SetupModulePhysicsSupport(Target);

			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"Chaos",
					"Engine",
					"EditorFramework",
					"UnrealEd",
					"PropertyEditor",
					"AnimGraphRuntime",
					"AnimGraph",
					"BlueprintGraph",
					"ToolMenus",
					"AnimGraphRuntime",
					"ChaosModularVehicleEngine"
				}
			);

			PrivateDefinitions.Add("CHAOS_INCLUDE_LEVEL_1=1");
		}
	}
}
