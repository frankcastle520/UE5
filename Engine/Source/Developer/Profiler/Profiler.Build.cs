// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using UnrealBuildTool;

[Obsolete("Profiler is deprecated since UE 5.0 - use Trace/UnrealInsights instead.")]
public class Profiler : ModuleRules
{
	public Profiler(ReadOnlyTargetRules Target) : base(Target)
	{
		UnsafeTypeCastWarningLevel = WarningLevel.Error;

		PublicDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"ApplicationCore",
				"InputCore",
				"RHI",
				"RenderCore",
				"Slate",
				"ProfilerClient",
				"DesktopPlatform",
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

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"SlateCore",
				"ToolWidgets"
			}
		);

		PrivateIncludePathModuleNames.AddRange(
			new string[] {
				"Messaging",
				"SessionServices",
			}
		);
	}
}
