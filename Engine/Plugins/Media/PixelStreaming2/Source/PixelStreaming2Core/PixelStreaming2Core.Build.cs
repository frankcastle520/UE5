// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;

namespace UnrealBuildTool.Rules
{
	public class PixelStreaming2Core : ModuleRules
	{
		public PixelStreaming2Core(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateDependencyModuleNames.AddRange(new string[]
			{
				"ApplicationCore",
				"Core",
				"CoreUObject",
				"DeveloperSettings",
				"Engine",
				"EngineSettings",
			});

			PublicDependencyModuleNames.AddRange(new string[]
			{
				"AVCodecsCore",
				"EpicRtc",
				"Slate"
			});

			if (Target.bBuildEditor)
			{
				PrivateDependencyModuleNames.AddRange(new string[]
				{
					"UnrealEd"
				});
			}
		}
	}
}
