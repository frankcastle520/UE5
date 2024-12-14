// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

[SupportedPlatforms(UnrealPlatformClass.All)]
public class AutoRTFMEngineTestsTarget : TargetRules
{
	public AutoRTFMEngineTestsTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Program;
		LinkType = TargetLinkType.Monolithic;
		LaunchModuleName = "AutoRTFMEngineTests";

		// No editor-only data is needed
		bBuildWithEditorOnlyData = false;

		bCompileAgainstEngine = true;
		bCompileAgainstCoreUObject = true;
		bCompileAgainstApplicationCore = true;
        bCompileWithPluginSupport = true;
        bBuildDeveloperTools = false;
        bBuildRequiresCookedData = true; // this program requires no data

		// No ICU internationalization as it causes shutdown errors.
		bCompileICU = false;

		// Don't need slate
		bUsesSlate = false;

		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		bIsBuildingConsoleApplication = true;
		bLegalToDistributeBinary = true;

		// Always want logging so we can see the test results
        bUseLoggingInShipping = true;

		// Network config
		bWithPushModel = true;		
		bUseIris = true;

		// Need to force enable tracing so we can test against it.
		bForceEnableTrace = true;

		// Load time profiling brings object construction to a crawl.
		GlobalDefinitions.Add("LOADTIMEPROFILERTRACE_ENABLED=0");

		if (!bGenerateProjectFiles)
		{
			bUseAutoRTFMCompiler = true;
		}
	}
}

