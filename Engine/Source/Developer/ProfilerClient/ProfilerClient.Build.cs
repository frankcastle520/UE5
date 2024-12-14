// Copyright Epic Games, Inc. All Rights Reserved.

using System;

namespace UnrealBuildTool.Rules
{
	[Obsolete("Profiler is deprecated since UE 5.0 - use Trace/UnrealInsights instead.")]
	public class ProfilerClient : ModuleRules
	{
		public ProfilerClient(ReadOnlyTargetRules Target) : base(Target)
		{
			UnsafeTypeCastWarningLevel = WarningLevel.Error;

			PublicIncludePathModuleNames.Add("ProfilerService");

			PublicDependencyModuleNames.AddRange(
				new string[] {
					"Core",
				}
			);

			PrivateDependencyModuleNames.AddRange(
				new string[] {
					"CoreUObject",
					"ProfilerMessages",
				}
			);

			PrivateIncludePathModuleNames.AddRange(
				new string[] {
					"Messaging",
					"MessagingCommon",
				}
			);
		}
	}
}
