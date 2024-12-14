// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class RigLogicMutable : ModuleRules
	{
		public RigLogicMutable(ReadOnlyTargetRules Target) : base(Target)
		{
			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"Engine",
					"CustomizableObject",
					"RigLogicModule",
				});

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
				});
		}
	}
}
