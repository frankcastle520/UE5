// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

[SupportedPlatforms(UnrealPlatformClass.Desktop)]
public class UbaObjToolTarget : TargetRules
{
	public UbaObjToolTarget(TargetInfo Target) : base(Target)
	{
		LaunchModuleName = "UbaObjTool";
		UbaAgentTarget.CommonUbaSettings(this, Target);
	}
}
