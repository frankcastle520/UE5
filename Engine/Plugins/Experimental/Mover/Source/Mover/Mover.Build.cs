// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Mover : ModuleRules
{
	public Mover(ReadOnlyTargetRules Target) : base(Target)
	{

		// TODO: find a better way to manage optional dependencies, such as Water and PoseSearch. This includes module dependencies here, as well as .uplugin dependencies.

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"NetCore",
				"InputCore",
				"NetworkPrediction",
				"AnimGraphRuntime",
				"MotionWarping",
				"Water",
				"GameplayTags"
			}
			);
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Chaos",
				"CoreUObject",
				"Engine",
				"PhysicsCore",
				"DeveloperSettings",
				"PoseSearch"
			}
			);

		SetupGameplayDebuggerSupport(Target);
	}
}
