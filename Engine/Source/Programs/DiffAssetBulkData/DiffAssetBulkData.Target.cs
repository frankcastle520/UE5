// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

public class DiffAssetBulkDataTarget : TargetRules
{
	public DiffAssetBulkDataTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Program;
		LinkType = TargetLinkType.Modular;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		LaunchModuleName = "DiffAssetBulkData";

		bUseXGEController					= false;
		bCompileFreeType					= false;
		bLoggingToMemoryEnabled				= false;
		bUseLoggingInShipping				= true;
		bCompileWithAccessibilitySupport	= false;
		bCompileWithPluginSupport			= false;
		bIncludePluginsForTargetPlatforms	= false;
		bWithServerCode						= false;
		bCompileNavmeshClusterLinks			= false;
		bCompileNavmeshSegmentLinks			= false;
		bCompileRecast						= false;
		bCompileICU 						= false;
		bWithLiveCoding						= false;
		bBuildDeveloperTools				= false;
		bBuildWithEditorOnlyData			= true;
		bCompileAgainstEngine				= false;
		bCompileAgainstCoreUObject			= true;
		bCompileAgainstApplicationCore		= true;
		bUsesSlate							= false;
		bIsBuildingConsoleApplication		= true;
		bForceBuildTargetPlatforms			= true;
		bNeedsExtraShaderFormats			= false;
		bAreTargetSDKVersionsRelevantOverride = false;

		WindowsPlatform.bUseBundledDbgHelp = false;
		WindowsPlatform.bPixProfilingEnabled = false;
		WindowsPlatform.bMergeIdenticalCOMDATs = true;
		WindowsPlatform.bStripUnreferencedSymbols = true;

		bEnableTrace = true;
		GlobalDefinitions.Add("UE_EXTERNAL_PROFILING_ENABLED=0");
	}
}
