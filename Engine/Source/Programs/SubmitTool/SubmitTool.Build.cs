// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;
public class SubmitTool : ModuleRules
{
	public SubmitTool(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicIncludePathModuleNames.Add("Launch");

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"AppFramework",
				"Core",
				"CoreUObject",
				"InputCore",
				"ApplicationCore",
				"Projects",
				"Slate",
				"SlateCore",
				"StandaloneRenderer",
				"ToolWidgets",
				"SourceControl",
				"DesktopPlatform", // Open file dialog
				"Settings",
				"OutputLog",
				"HTTP",
				"Json",
				"JsonUtilities",
				"Analytics",
				"AnalyticsET",
				"PakFile"
			}
		);

		DynamicallyLoadedModuleNames.AddRange(
			new string[] {
				"PerforceSourceControl",
			}
		);


		if (Target.Configuration != UnrealTargetConfiguration.Shipping)
		{
			PrivateIncludePathModuleNames.AddRange(
				new string[] {
				"SlateReflector",
				}
			);

			DynamicallyLoadedModuleNames.AddRange(
				new string[] {
				"SlateReflector",
				}
			);
		}

		AddEngineThirdPartyPrivateStaticDependencies(Target, "Perforce");

		if (Target.IsInPlatformGroup(UnrealPlatformGroup.Linux))
		{
			PrivateDependencyModuleNames.AddRange(
				new string[] {
					"UnixCommonStartup"
				}
			);
		}
		
		RuntimeDependencies.Add("$(EngineDir)/Content/Editor/Slate/...*.png", StagedFileType.UFS);
	}
}
