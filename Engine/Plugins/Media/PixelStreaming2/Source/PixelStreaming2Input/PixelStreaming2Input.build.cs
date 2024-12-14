// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class PixelStreaming2Input : ModuleRules
	{
		public PixelStreaming2Input(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"Engine",
					"InputDevice",
					"Json",
					"SlateCore",
					"Slate",
					"DeveloperSettings",
					"HeadMountedDisplay",
					"XRBase",
					"PixelStreaming2Core"
				}
			);

			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"ApplicationCore",
					"PixelStreaming2HMD",
					"InputCore",
					"Core"
				}
			);
		}
	}
}