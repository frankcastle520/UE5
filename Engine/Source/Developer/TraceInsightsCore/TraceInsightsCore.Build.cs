// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class TraceInsightsCore : ModuleRules
{
	public TraceInsightsCore(ReadOnlyTargetRules Target) : base(Target)
	{
		UnsafeTypeCastWarningLevel = WarningLevel.Error;

		PublicDependencyModuleNames.AddRange(
			new string[] {
				"ApplicationCore",
				"Core",
				"CoreUObject",
				"DesktopPlatform",
				"InputCore",
				"Slate",
				"SlateCore",
				"ToolWidgets",
				"TraceServices",
			}
		);
	}
}
