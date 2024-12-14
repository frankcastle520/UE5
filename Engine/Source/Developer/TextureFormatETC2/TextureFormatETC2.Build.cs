// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class TextureFormatETC2 : ModuleRules
{
	public TextureFormatETC2(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateIncludePathModuleNames.AddRange(new string[]
		{
			"TextureCompressor",
			"TextureFormat",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"DerivedDataCache",
			"ImageCore",
			"TextureBuild",
		});

		AddEngineThirdPartyPrivateStaticDependencies(Target, "etc2comp");
	}
}
