// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using EpicGames.Core;

namespace UnrealBuildTool.Rules
{
	public class RigLogicLibTest : ModuleRules
	{
		public RigLogicLibTest(ReadOnlyTargetRules Target) : base(Target)
		{
			bUseUnity = false; // A windows include is preprocessing some method names causing compile failures.
			bDisableStaticAnalysis = true;

			PrivateDefinitions.Add("RL_BUILD_WITH_XYZ_ROTATION_ORDER=1");

			if (Target.Platform == UnrealTargetPlatform.Win64 && Target.Architecture == UnrealArch.X64)
			{
				PrivateDefinitions.Add("RL_BUILD_WITH_SSE=1");
				PublicDefinitions.Add("GTEST_OS_WINDOWS=1");
			}

			string RigLogicLibPath = Path.GetFullPath(Path.Combine(ModuleDirectory, "../RigLogicLib"));

			if (Target.LinkType == TargetLinkType.Monolithic || Target.bMergeModules)
			{
				PublicDependencyModuleNames.Add("RigLogicLib");
				PrivateIncludePaths.Add(Path.Combine(RigLogicLibPath, "Private"));
			}
			else
			{
				PrivateDefinitions.Add("RIGLOGIC_MODULE_DISCARD");
				ConditionalAddModuleDirectory(new DirectoryReference(RigLogicLibPath));
			}

			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"GoogleTest"
				}
			);
		}
	}
}
