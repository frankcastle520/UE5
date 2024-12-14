// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

[SupportedPlatforms(UnrealPlatformClass.All)]
public class ReplicationSystemLowLevelTestsTarget : TestTargetRules
{
	public ReplicationSystemLowLevelTestsTarget(TargetInfo Target) : base(Target)
	{
		bCompileAgainstEngine = true;
		bCompileAgainstApplicationCore = true;
		bUsesSlate = false;

		bUsePlatformFileStub = true;
		bMockEngineDefaults = true;

		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;

		// Network config
		bWithPushModel = true;
		bUseIris = true;

		bEnableTrace = true;
		GlobalDefinitions.Add("UE_NET_TEST_FAKE_REP_TAGS=1");
		// Load time profiling brings object construction to a crawl.
		GlobalDefinitions.Add("LOADTIMEPROFILERTRACE_ENABLED=0");
	}
}
