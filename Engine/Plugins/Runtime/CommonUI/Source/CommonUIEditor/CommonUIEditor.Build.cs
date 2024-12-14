// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class CommonUIEditor : ModuleRules
{
	public CommonUIEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(
		new string[]
			{
				"Core",
				"CoreUObject",
				"ApplicationCore",
				"Engine",
				"PropertyEditor",
				"InputCore",
				"Slate",
				"UMG",
				"SlateCore",
				"CommonUI",
                "EditorWidgets",	
				"UnrealEd",
				"GameplayTags",
				"GameplayTagsEditor",
				"AssetTools",
				"AssetRegistry",
				"ToolMenus",
			}
        );

		PrivateIncludePathModuleNames.AddRange(
			new string[] {
				"DataTableEditor",
			}
		);

		PublicIncludePaths.AddRange(
			new string[]
			{
			}
		);
	}
}
