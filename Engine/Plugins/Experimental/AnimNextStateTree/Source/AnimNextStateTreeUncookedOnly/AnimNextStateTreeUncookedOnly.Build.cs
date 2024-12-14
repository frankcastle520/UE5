// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class AnimNextStateTreeUncookedOnly : ModuleRules
	{
		public AnimNextStateTreeUncookedOnly(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateDependencyModuleNames.AddRange(new string[] 
			{
				"StateTreeEditorModule",
				"RigVM",
				"RigVMDeveloper"
			});
			
			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"StateTreeModule",
					"Engine",
					"WorkspaceEditor",
					"SlateCore",
					"AnimNext",
					"AnimNextUncookedOnly",
					"AnimNextStateTree",
					"AnimNextAnimGraph"
				}
			);
		}
	}
}