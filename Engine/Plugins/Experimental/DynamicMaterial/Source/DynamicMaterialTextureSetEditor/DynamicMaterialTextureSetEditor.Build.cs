// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class DynamicMaterialTextureSetEditor : ModuleRules
{
	public DynamicMaterialTextureSetEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"UnrealEd"
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"AssetDefinition",
				"Core",
				"DeveloperSettings",
				"DynamicMaterialTextureSet",
				"EditorWidgets",
				"Engine",
				"InputCore",
				"PropertyEditor",
				"Slate",
				"SlateCore",
			}
		);

		ShortName = "DynMatTexSetEd";
	}
}
