// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class TraceInsights : ModuleRules
{
	public TraceInsights(ReadOnlyTargetRules Target) : base(Target)
	{
		UnsafeTypeCastWarningLevel = WarningLevel.Error;

		PublicDependencyModuleNames.AddRange(
			new string[] {
				"AppFramework", // for SColorPicker, RestoreStarshipSuite()
				"ApplicationCore",
				"AutomationDriver",
				"Cbor",
				"Core",
				"CoreUObject",
				"DesktopPlatform",
				"InputCore",
				"RenderCore",
				"RHI",
				"SessionServices",
				"Slate",
				"Sockets",
				"SourceCodeAccess",
				"TraceAnalysis",
				"TraceInsightsCore",
				"TraceLog",
				"TraceTools",
				"TraceServices",
				"WorkspaceMenuStructure",
				"XmlParser",
			}
		);

		// Modules required for running automation in stand alone Insights
		if (Target.Configuration != UnrealTargetConfiguration.Shipping && !Target.bBuildEditor)
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

		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[] {
					"Engine",
				}
			);
		}

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"ImageCore",
				"SlateCore",
				"ToolWidgets"
			}
		);

		PrivateIncludePathModuleNames.AddRange(
			new string[] {
				"Messaging",
				"MessageLog",
			}
		);
	}
}
