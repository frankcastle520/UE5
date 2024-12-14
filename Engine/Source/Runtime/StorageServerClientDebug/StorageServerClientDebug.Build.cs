// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class StorageServerClientDebug : ModuleRules
{
	public StorageServerClientDebug(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"Engine",		// For on-screen debug messages
				"Sockets",
				"StorageServerClient",
				"Json"
			}
		);
	}
}
