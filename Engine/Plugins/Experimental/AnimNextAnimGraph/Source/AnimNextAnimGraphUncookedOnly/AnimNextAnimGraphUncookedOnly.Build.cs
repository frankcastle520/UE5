// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class AnimNextAnimGraphUncookedOnly : ModuleRules
	{
		public AnimNextAnimGraphUncookedOnly(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"Engine",
					"AnimNext",
					"AnimNextUncookedOnly",
					"AnimNextAnimGraph",
					"ControlRigDeveloper",
					"BlueprintGraph",	// For K2 schema
					"AnimGraph",
					"RigVM",
					"ControlRig",
				}
			);

			if (Target.bBuildEditor)
			{
				PrivateDependencyModuleNames.AddRange(
					new string[]
					{
						"UnrealEd",
						"BlueprintGraph",
					}
				);
			}
		}
	}
}