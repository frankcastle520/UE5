// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class DiffAssetBulkData : ModuleRules
{
	public DiffAssetBulkData(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicIncludePathModuleNames.Add("Launch");

		PrivateDependencyModuleNames.AddRange(new string[] {
			"ApplicationCore",
			"AssetRegistry",
			"Core", 
			"CoreUObject",
			"Json",
			"Projects", 
		});
	}
}
