// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;

namespace UnrealBuildTool.Rules
{
	[SupportedPlatformGroups("Windows")]
	public class PixWinPlugin : ModuleRules
	{
		public PixWinPlugin(ReadOnlyTargetRules Target) : base(Target)
        {
			PublicDependencyModuleNames.AddAll(
				"InputDevice",
				"RenderCore"
			);

			PrivateDependencyModuleNames.AddAll(
				"Core",
				"CoreUObject",
				"Engine",
				"InputCore",
				"Projects",
				"RHI"
			);

			if (Target.Configuration != UnrealTargetConfiguration.Shipping)
			{
				PrivateDependencyModuleNames.Add("WinPixEventRuntime");
			}

			if (Target.bBuildEditor == true)
			{
				DynamicallyLoadedModuleNames.Add("LevelEditor");

				PrivateDependencyModuleNames.AddAll(
					"Slate"
					, "SlateCore"
					, "EditorFramework"
					, "UnrealEd"
					, "MainFrame"
					, "GameProjectGeneration"
				);
			}
		}
	}
}
