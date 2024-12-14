// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class StylusInputDebugWidget : ModuleRules
	{
		public StylusInputDebugWidget(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"Engine",
					"Slate",
					"SlateCore",
					"StylusInput",
					"UnrealEd",
					"WorkspaceMenuStructure",
				}
			);
		}
	}
}
