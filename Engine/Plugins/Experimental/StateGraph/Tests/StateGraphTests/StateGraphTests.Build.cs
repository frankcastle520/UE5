// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class StateGraphTests : TestModuleRules
{
	static StateGraphTests()
	{
		TestMetadata = new Metadata();
		TestMetadata.TestName = "StateGraph";
		TestMetadata.TestShortName = "State Graph";
	}

	public StateGraphTests(ReadOnlyTargetRules Target) : base(Target)
	{
		OptimizeCode = CodeOptimization.Never;

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"StateGraph"
			});
	}
}
