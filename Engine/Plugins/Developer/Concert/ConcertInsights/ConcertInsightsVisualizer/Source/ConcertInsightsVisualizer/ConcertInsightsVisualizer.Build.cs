// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ConcertInsightsVisualizer : ModuleRules
{
	public ConcertInsightsVisualizer(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
			});
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Projects",
				"Slate",
				"SlateCore",
				"TraceAnalysis",
				"TraceInsights",
				"TraceServices"
			});
		
		ShortName = "CrtInV";
	}
}
