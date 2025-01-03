// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class UnixCommonStartup : ModuleRules
{
	public UnixCommonStartup(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.Add("Core");
		PrivateDependencyModuleNames.Add("ApplicationCore");
		PrivateDependencyModuleNames.Add("Projects");

		PrivateDependencyModuleNames.Add("Slate");

		// Only include Engine if necessary
		if (Target.bCompileAgainstEngine)
		{
			PublicDependencyModuleNames.Add("Engine");
		}

		if (Target.Type == TargetType.Editor)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[] {
					"SourceControl",
				}
			);
		}
	}
}
