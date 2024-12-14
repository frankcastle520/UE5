// Copyright Epic Games, Inc. All Rights Reserved.

using System;

namespace UnrealBuildTool.Rules
{
	[Obsolete("Profiler is deprecated since UE 5.0 - use Trace/UnrealInsights instead.")]
	public class ProfilerService : ModuleRules
	{
		public ProfilerService(ReadOnlyTargetRules Target) : base(Target)
		{
			PublicDependencyModuleNames.AddRange(
				new string[] {
					"Core",
				}
			);

			PrivateDependencyModuleNames.AddRange(
				new string[] {
					"CoreUObject",
					"Engine",
					"ProfilerMessages",
				}
			);

			PrivateIncludePathModuleNames.AddRange(
				new string[] {
					"MessagingCommon",
				}
			);

			if (Target.Configuration != UnrealTargetConfiguration.Shipping)
			{
				PrecompileForTargets = PrecompileTargetsType.Any;
			}
		}
	}
}
