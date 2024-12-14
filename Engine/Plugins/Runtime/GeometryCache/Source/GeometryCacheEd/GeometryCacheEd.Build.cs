// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class GeometryCacheEd : ModuleRules
{
	public GeometryCacheEd(ReadOnlyTargetRules Target) : base(Target)
	{
        PublicDependencyModuleNames.AddRange(
			new string[] {
				"AssetDefinition",
				"Core",
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore",
                "InputCore",
                "RenderCore",
                "RHI",
				"EditorFramework",
				"EditorWidgets",
				"UnrealEd",
				"AssetTools",
                "GeometryCache",
				"ToolMenus",
				"NiagaraEditor",
				"AdvancedPreviewScene",
				"SequencerWidgets",
				"TimeManagement"
			}
		);

		UnsafeTypeCastWarningLevel = WarningLevel.Error;
	}
}
