// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class AnimNextAnimGraphTestSuite : ModuleRules
	{
		public AnimNextAnimGraphTestSuite(ReadOnlyTargetRules Target) : base(Target)
		{
			PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"Engine",
					"AnimNext",
					"AnimNextTestSuite",
					"AnimNextAnimGraph",
					"RigVM",
				}
			);

			if (Target.bBuildEditor == true)
			{
				PrivateDependencyModuleNames.AddRange(
					new string[]
					{
						"AnimNextUncookedOnly",
						"AnimNextEditor",
						"RigVMDeveloper",
					}
				);
			}
		}
	}
}