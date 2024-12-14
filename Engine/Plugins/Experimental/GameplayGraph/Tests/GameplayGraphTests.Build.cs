// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class GameplayGraphTests : TestModuleRules
{
	static GameplayGraphTests()
	{
		TestMetadata = new Metadata()
		{
			TestName = "GameplayGraph",
			TestShortName = "GameplayGraph"
		};
	}

	public GameplayGraphTests(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"GameplayGraph"
			});
	}
}