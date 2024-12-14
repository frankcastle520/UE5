// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class CaptureDataEditor : ModuleRules
{
	public CaptureDataEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"AssetDefinition",
			"UnrealEd",
			"CaptureDataCore",
			"PropertyEditor",
			"SlateCore",
			"Slate",
			"InputCore",
		});
	}
}
