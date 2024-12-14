// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class AudioPlatformSupportWasapi : ModuleRules
	{
		public AudioPlatformSupportWasapi(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"Core"
				}
			);
		}
	}
}
