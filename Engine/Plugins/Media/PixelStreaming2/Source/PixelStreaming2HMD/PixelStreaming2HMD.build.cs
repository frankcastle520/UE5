// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class PixelStreaming2HMD : ModuleRules
	{
		public PixelStreaming2HMD(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"Engine",
					"InputCore",
					"RHI",
					"RenderCore",
					"Renderer",
					"HeadMountedDisplay",
					"XRBase",
					"SlateCore",
					"PixelStreaming2Core"
				}
			);
		}
	}
}