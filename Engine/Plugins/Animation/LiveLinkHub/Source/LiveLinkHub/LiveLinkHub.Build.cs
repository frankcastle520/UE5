// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class LiveLinkHub : ModuleRules
{
	public LiveLinkHub(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
			});

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"ApplicationCore",
				"AssetRegistry",
				"AssetTools",
				"ContentBrowser",
				"ContentBrowserAssetDataSource",
				"ContentBrowserData",
				"EditorStyle",
				"Engine",
				"InputCore",
				"Json",
				"JsonUtilities",
				"LiveLink",
				"LiveLinkEditor",
				"LiveLinkHubMessaging",
				"LiveLinkInterface",
				"LiveLinkMessageBusFramework",
				"Messaging",
				"ProjectSettingsViewer",
				"SharedSettingsWidgets",
				"Settings",
				"SettingsEditor",
				"Slate",
				"SlateCore",
				"StandaloneRenderer",
				"TimeManagement",
				"ToolMenus",
				"ToolWidgets",
				"UnrealEd",
				"OutputLog",
			});

		if (Target.Configuration != UnrealTargetConfiguration.Shipping)
		{
			PrivateIncludePathModuleNames.AddRange(
				new string[] {
					"SlateReflector",
				});

			DynamicallyLoadedModuleNames.AddRange(
				new string[] {
					"SlateReflector",
				});

			if (Target.bBuildTargetDeveloperTools)
			{
				PrivateDependencyModuleNames.AddRange(
					new string[] {
						"SessionServices",
						"SessionFrontend",
					});
			}
		}
	}
}
