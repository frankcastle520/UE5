// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class uLangUE : ModuleRules
{
	public uLangUE( ReadOnlyTargetRules Target ) : base(Target)
	{
		BinariesSubFolder = "NotForLicensees";

		PublicDependencyModuleNames.AddRange(new string[]{
			"uLangCore",
		});

		PrivateDependencyModuleNames.AddRange(new string[]{
			"Core",
		});
	}
}
