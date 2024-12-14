// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class EngineCameras : ModuleRules
{
	public EngineCameras(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "Legacy"));

		PublicDependencyModuleNames.AddRange(
			new string[] {
				"CinematicCamera",
				"Core",
				"CoreUObject",
				"Engine",
				"HeadMountedDisplay",
				"MovieScene",
				"MovieSceneTracks",
				"TemplateSequence",
				"TraceLog"
			}
		);
	}
}
