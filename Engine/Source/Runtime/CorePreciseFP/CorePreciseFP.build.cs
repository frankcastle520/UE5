// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class CorePreciseFP : ModuleRules
{
	public CorePreciseFP(ReadOnlyTargetRules Target) : base(Target)
	{
		FPSemantics = FPSemanticsMode.Precise;
		PCHUsage = PCHUsageMode.NoPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
			}
		);
	}
}
