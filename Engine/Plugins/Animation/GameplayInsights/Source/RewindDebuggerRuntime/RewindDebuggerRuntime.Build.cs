// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class RewindDebuggerRuntime : ModuleRules
	{
		public RewindDebuggerRuntime(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"RewindDebuggerRuntimeInterface",
				"TraceLog",
			});
		}
	}
}

