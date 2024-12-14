// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

[SupportedPlatforms("Win64", "Linux", "Mac")]
public class IasToolTarget : TargetRules
{
	public IasToolTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Program;
		LinkType = TargetLinkType.Monolithic;
		LaunchModuleName = "IasTool";
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;

		GlobalDefinitions.Add("UE_WITH_IAS_TOOL=1");

		bBuildDeveloperTools = false;
		bBuildWithEditorOnlyData = false;
		bCompileAgainstApplicationCore = false;
		bCompileAgainstCoreUObject = false;
		bCompileAgainstEngine = false;
		bCompileICU = false;
		bCompileICU = false;
		bCompileNavmeshClusterLinks = false;
		bCompileNavmeshSegmentLinks = false;
		bCompileRecast = false;
		bCompileWithPluginSupport = true;
		bForceEnableExceptions = true;
		bIsBuildingConsoleApplication = true;
		bUsesSlate = false;
		bWithLiveCoding = false;
		bWithServerCode = false;
	}
}
