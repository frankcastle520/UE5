// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ChaosUserDataPTTests : TestModuleRules
{
	static ChaosUserDataPTTests()
	{
		if (InTestMode)
		{
			TestMetadata = new Metadata();
			TestMetadata.TestName = "ChasoUserDataPT";
			TestMetadata.TestShortName = "Chaos User Data PT";
		}
	}

	public ChaosUserDataPTTests(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"ApplicationCore",
				"Core",
				//"CoreUObject",
				"Chaos",
				"ChaosUserDataPT"
			});

		//PrivateIncludePaths.Add("FortniteGame/Private");
		//PublicIncludePaths.Add("FortniteGame/Public");
	}
}