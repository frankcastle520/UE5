// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class AnimNextAnimGraph : ModuleRules
	{
		public AnimNextAnimGraph(ReadOnlyTargetRules Target) : base(Target)
		{
			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"RigVM",
					"Engine",
					"AnimNext",
					"Chooser",
				}
			);
		}
	}
}