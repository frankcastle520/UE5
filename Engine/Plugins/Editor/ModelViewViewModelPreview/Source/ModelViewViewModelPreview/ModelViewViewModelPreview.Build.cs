// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ModelViewViewModelPreview : ModuleRules
{
	public ModelViewViewModelPreview(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateIncludePathModuleNames.AddRange(
			new string[]
			{
				"UMGWidgetPreview",
			});

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"AdvancedWidgets",
				"Core",
				"CoreUObject",
				"InputCore",
				"ModelViewViewModel",
				"ModelViewViewModelBlueprint",
				"Projects",
				"Slate",
				"SlateCore",
				"UMGWidgetPreview",
			});
	}
}
