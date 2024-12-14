// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class IoStoreUtilities : ModuleRules
{
	public IoStoreUtilities (ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateIncludePathModuleNames.AddRange(new string[] {
			"StorageServerClient",
			"TargetPlatform",
		});

		PrivateDependencyModuleNames.AddRange(new string[] {
			"AssetRegistry",
			"CookMetadata",
			"Core",
			"CoreUObject",
			"DerivedDataCache",
			"DeveloperToolSettings",
			"Json",
			"PakFile",
			"Projects",
			"RenderCore",
			"RSA",
			"SandboxFile",
			"Sockets",
			"Zen",
		});

		PublicIncludePathModuleNames.AddRange(new string[] {
			"Zen",
		});
	}
}
