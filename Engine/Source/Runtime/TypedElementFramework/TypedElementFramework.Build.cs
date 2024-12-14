// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using System.IO;
using UnrealBuildTool;

public class TypedElementFramework : ModuleRules
{
	public TypedElementFramework(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "Tests"));

		PrivateDependencyModuleNames.AddAll(
			"SlateCore"
		);
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
			}
		);
    }
}
