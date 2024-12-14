// Copyright Epic Games, Inc. All Rights Reserved.

using System;

namespace UnrealBuildTool.Rules
{
	[Obsolete("Profiler is deprecated since UE 5.0 - use Trace/UnrealInsights instead.")]
	public class ProfilerMessages : ModuleRules
	{
		public ProfilerMessages(ReadOnlyTargetRules Target) : base(Target)
		{
			PublicDependencyModuleNames.AddRange(
				new string[] {
					"CoreUObject",
				});

			PrivateDependencyModuleNames.AddRange(
				new string[] {
					"Core",
				});

			if (Target.Configuration != UnrealTargetConfiguration.Shipping)
			{
				PrecompileForTargets = PrecompileTargetsType.Any;
			}
		}
	}
}
