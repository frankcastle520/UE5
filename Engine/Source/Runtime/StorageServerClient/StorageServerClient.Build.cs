// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class StorageServerClient : ModuleRules
{
	public StorageServerClient(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"Sockets",
				"CookOnTheFly",
				"Json"
			}
		);
	}
}
