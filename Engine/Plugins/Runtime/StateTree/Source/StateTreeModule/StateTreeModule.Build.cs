// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class StateTreeModule : ModuleRules
	{
		public StateTreeModule(ReadOnlyTargetRules Target) : base(Target)
		{
			UnsafeTypeCastWarningLevel = WarningLevel.Warning;

			PublicIncludePaths.AddRange(
				new string[] {
				}
			);

			PublicDependencyModuleNames.AddRange(
				new [] {
					"Core",
					"CoreUObject",
					"DeveloperSettings",
					"Engine",
					"AIModule",
					"GameplayTags"
				}
			);

			PrivateDependencyModuleNames.AddRange(
				new [] {
					"PropertyPath",
				}
			);

			UnsafeTypeCastWarningLevel = WarningLevel.Error;

			if (Target.bBuildEditor)
			{
				PublicDependencyModuleNames.AddRange(
					new [] {
						"UnrealEd",
						"BlueprintGraph",
					}
				);
				PrivateDependencyModuleNames.AddRange(
					new [] {
						"StructUtilsEditor",
						"EditorSubsystem",
						"EditorFramework"
					}
				);
			}

			// Allow debugger traces on all non-shipping targets and shipping editors (UEFN)
			if (Target.Configuration != UnrealTargetConfiguration.Shipping || Target.bBuildEditor)
			{
				PublicDefinitions.Add("WITH_STATETREE_TRACE=1");
				PublicDependencyModuleNames.AddRange(
					new []
					{
						"TraceLog"
					}
				);
				
				// Allow debugger trace analysis on editor platforms
				if (Target.Platform.IsInGroup(UnrealPlatformGroup.Desktop) && Target.bBuildEditor)
				{
					PublicDefinitions.Add("WITH_STATETREE_TRACE_DEBUGGER=1");
					PublicDependencyModuleNames.AddRange(
						new []
						{
							"TraceServices",
							"TraceAnalysis"
						}
					);
				}
				else
				{
					PublicDefinitions.Add("WITH_STATETREE_TRACE_DEBUGGER=0");
				}
			}
			else
			{
				PublicDefinitions.Add("WITH_STATETREE_TRACE=0");
				PublicDefinitions.Add("WITH_STATETREE_TRACE_DEBUGGER=0");
			}
		}
	}
}
