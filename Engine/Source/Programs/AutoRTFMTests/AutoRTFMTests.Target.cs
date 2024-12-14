// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using UnrealBuildTool;
using System.Collections.Generic;

[SupportedPlatforms(UnrealPlatformClass.All)]
public class AutoRTFMTestsTarget : TargetRules
{
	// TODO: Might be useful to promote this to a general Target.cs setting at some point in the future.
	[CommandLine("-AllowLogFile")]
	public bool bAllowLogFile = false;

	public AutoRTFMTestsTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Program;
		LinkType = TargetLinkType.Monolithic;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;

		Name = "AutoRTFMTests";
		LaunchModuleName = "AutoRTFMTests";

		bCompileAgainstEngine = false;
		bCompileAgainstCoreUObject = true;

		// Logs are still useful to print the results
		bUseLoggingInShipping = true;

		// Make a console application under Windows, so entry point is main() everywhere
		bIsBuildingConsoleApplication = true;

		// Disable unity builds by default for AutoRTFMTest
		bUseUnityBuild = false;

		// Set the RTFM clang compiler
		if (!bGenerateProjectFiles)
		{
			 bUseAutoRTFMCompiler = true;
		}

		bFNameOutlineNumber = true;

		MinCpuArchX64 = MinimumCpuArchitectureX64.AVX;

		bCompileWithStatsWithoutEngine = true;
		GlobalDefinitions.Add("ENABLE_STATNAMEDEVENTS=1");
		GlobalDefinitions.Add("ENABLE_STATNAMEDEVENTS_UOBJECT=1");

		// Allow for disabling writing out the logfile, since in `PreSubmitTest.py` we run this target simultaneously
		// multiple times, and doing so would cause writing them out to stomp each other.
		if (!bAllowLogFile)
		{
			GlobalDefinitions.Add("ALLOW_LOG_FILE=0");
		}
		else
		{
			GlobalDefinitions.Add("ALLOW_LOG_FILE=1");
		}

		GlobalDefinitions.Add("MALLOC_LEAKDETECTION=1");
		GlobalDefinitions.Add("PLATFORM_USES_FIXED_GMalloc_CLASS=0");
	}
}
