// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class AudioMixerWasapi : ModuleRules
{
	public AudioMixerWasapi(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateIncludePathModuleNames.Add("TargetPlatform");

		if (Target.bCompileAgainstEngine)
        {
            PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"Engine",
					"AudioPlatformSupportWasapi",
					"WindowsMMDeviceEnumeration"
				}
			);
        }

        PrivateDependencyModuleNames.AddRange(
			new string[] {
					"Core",
					"CoreUObject",
					"AudioMixer",
					"AudioMixerCore"
                }
		);

		PrecompileForTargets = PrecompileTargetsType.None;

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PrecompileForTargets = PrecompileTargetsType.Any;
		}
	}
}
