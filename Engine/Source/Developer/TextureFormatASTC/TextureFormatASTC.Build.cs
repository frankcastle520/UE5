// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class TextureFormatASTC : ModuleRules
{
	public TextureFormatASTC(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateIncludePathModuleNames.AddRange(new string[]
		{
			"TextureCompressor",
			"TextureFormat",
		});
		
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"DerivedDataCache", // for FCacheBucket
			"ImageCore",
			"ImageWrapper",
			"TextureBuild",
			"TextureFormatIntelISPCTexComp",
			"astcenc",
		});
	}
}
