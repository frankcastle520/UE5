// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class TedsContentBrowser : ModuleRules
{
	public TedsContentBrowser(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		if (Target.bBuildEditor)
		{
			PublicIncludePaths.AddRange(new string[] {});
			PrivateIncludePaths.AddRange(new string[] {});

			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject"
				});
			
			PrivateDependencyModuleNames.AddRange(
            	new string[]
            	{
		            "TypedElementFramework",
		            "SlateCore",
		            "Slate",
		            "TedsTableViewer",
		            "ContentBrowser",
		            "ContentBrowserData",
		            "InputCore",
		            "TedsAssetData",
		            "ToolWidgets"
            	});
			
			DynamicallyLoadedModuleNames.AddRange(new string[] {});
		}
	}
}
