// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

[SupportedPlatforms()]
public class UbaCommonTarget : TargetRules
{
	public UbaCommonTarget(TargetInfo Target) : base(Target)
	{
		LaunchModuleName = "UbaCommon";
		bShouldCompileAsDLL = true;
		UbaAgentTarget.CommonUbaSettings(this, Target);
	}
}
