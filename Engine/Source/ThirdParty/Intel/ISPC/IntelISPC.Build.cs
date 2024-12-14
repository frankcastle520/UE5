// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class IntelISPC : ModuleRules
{
	public IntelISPC(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		PublicDefinitions.Add($"INTEL_ISPC={(Target.bCompileISPC ? 1 : 0)}");
	}
}