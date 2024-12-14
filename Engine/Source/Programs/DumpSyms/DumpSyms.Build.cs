// Copyright Epic Games, Inc. All Rights Reserved.

using System.Globalization;
using System.IO;
using UnrealBuildBase;
using UnrealBuildTool;

public class DumpSyms : ModuleRules
{
	public DumpSyms(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicIncludePathModuleNames.Add("Launch");

		bAddDefaultIncludePaths = false;

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			string ToolChainDir = Target.WindowsPlatform.ToolChainDir;
			PrivateIncludePaths.Add(Path.Combine(ToolChainDir, "atlmfc", "include"));
			PublicAdditionalLibraries.Add(Path.Combine(ToolChainDir, "atlmfc", "lib", "x64", "atls.lib"));

			string DiaSdkDir = Target.WindowsPlatform.DiaSdkDir;
			PrivateIncludePaths.Add(Path.Combine(DiaSdkDir, "include"));
			PublicAdditionalLibraries.Add(Path.Combine(DiaSdkDir, "lib", "amd64", "diaguids.lib"));
			RuntimeDependencies.Add("$(TargetOutputDir)/msdia140.dll", Path.Combine(DiaSdkDir, "bin", "amd64", "msdia140.dll"));

			string WindowsSdkDir = Target.WindowsPlatform.WindowsSdkDir;
			PublicAdditionalLibraries.Add(Path.Combine(WindowsSdkDir, "Lib", Target.WindowsPlatform.WindowsSdkVersion, "um", "x64", "imagehlp.lib"));
		}

		PrivateIncludePaths.AddRange(
			new string[]
			{
				Path.Combine(EngineDirectory, "Source", "ThirdParty", "Breakpad", "src"),
				Path.Combine(EngineDirectory, "Source", "ThirdParty", "Breakpad", "src", "third_party", "llvm"),
			});

		PrivateDefinitions.Add("_LIBCXXABI_DISABLE_VISIBILITY_ANNOTATIONS");

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"mimalloc"
			}
		);

		PrivateDependencyModuleNames.Add("zlib");

		bUseRTTI = true;
	}
}
