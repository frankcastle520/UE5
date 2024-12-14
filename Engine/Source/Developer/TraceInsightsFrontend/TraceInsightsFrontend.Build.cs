// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class TraceInsightsFrontend : ModuleRules
{
	public TraceInsightsFrontend(ReadOnlyTargetRules Target) : base(Target)
	{
		UnsafeTypeCastWarningLevel = WarningLevel.Error;

		PublicDependencyModuleNames.AddRange(
			new string[] {
				"AppFramework", // for RestoreStarshipSuite()
				"ApplicationCore",
				"AutomationDriver",
				"Core",
				"CoreUObject",
				"DesktopPlatform",
				"InputCore",
				"Slate",
				"SlateCore",
				"Sockets",
				"TraceAnalysis",
				"TraceInsightsCore",
				"TraceServices",
				"WorkspaceMenuStructure",
			}
		);

		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[] {
					"Engine",
				}
			);
		}

		// Modules required for running automation
		if (!Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[] {
					"AutomationWorker",
					"AutomationController",
					"AutomationWindow",
					"SessionServices",
				}
			);
		}
	}
}
