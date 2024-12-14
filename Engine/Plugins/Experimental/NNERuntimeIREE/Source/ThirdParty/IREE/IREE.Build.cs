// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class IREE : ModuleRules
{
	public IREE(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		PublicSystemIncludePaths.Add(System.IO.Path.Combine(ModuleDirectory, "Include"));

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PublicAdditionalLibraries.Add(System.IO.Path.Combine(ModuleDirectory, "Lib", "Windows", "flatcc_parsing.lib"));
			PublicAdditionalLibraries.Add(System.IO.Path.Combine(ModuleDirectory, "Lib", "Windows", "ireert.lib"));

			if (Target.WindowsPlatform.Compiler.IsClang())
			{
				PublicSystemIncludePaths.Add(System.IO.Path.Combine(ModuleDirectory, "Include", "Clang"));
			}
		}
		else if (Target.Platform == UnrealTargetPlatform.Linux)
		{
			PublicAdditionalLibraries.Add(System.IO.Path.Combine(ModuleDirectory, "Lib", "Linux", "libflatcc_parsing.a"));
			PublicAdditionalLibraries.Add(System.IO.Path.Combine(ModuleDirectory, "Lib", "Linux", "libireert.a"));
		}
		else if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			PublicAdditionalLibraries.Add(System.IO.Path.Combine(ModuleDirectory, "Lib", "Mac", "libflatcc_parsing.a"));
			PublicAdditionalLibraries.Add(System.IO.Path.Combine(ModuleDirectory, "Lib", "Mac", "libireert.a"));
		}

		if (Target.Type == TargetType.Editor)
		{
			string BinariesPath = Path.Combine(PluginDirectory, "Binaries", "ThirdParty", "IREE");

			if (Target.Platform == UnrealTargetPlatform.Win64)
			{
				RuntimeDependencies.Add(Path.Combine(BinariesPath, "Windows", "clang++.exe"));
				RuntimeDependencies.Add(Path.Combine(BinariesPath, "Windows", "iree-compile.exe"));
				RuntimeDependencies.Add(Path.Combine(BinariesPath, "Windows", "IREECompiler.dll"));
				RuntimeDependencies.Add(Path.Combine(BinariesPath, "Windows", "ld.lld.exe"));
			}
			else if (Target.Platform == UnrealTargetPlatform.Linux)
			{
				RuntimeDependencies.Add(Path.Combine(BinariesPath, "Linux", "iree-compile"));
				RuntimeDependencies.Add(Path.Combine(BinariesPath, "Linux", "libIREECompiler.so"));
				RuntimeDependencies.Add(Path.Combine(BinariesPath, "Linux", "ld.lld"));
			}
			else if (Target.Platform == UnrealTargetPlatform.Mac)
			{
				RuntimeDependencies.Add(Path.Combine(BinariesPath, "Mac", "iree-compile"));
				RuntimeDependencies.Add(Path.Combine(BinariesPath, "Mac", "libIREECompiler.dylib"));
				RuntimeDependencies.Add(Path.Combine(BinariesPath, "Mac", "ld.lld"));
				RuntimeDependencies.Add(Path.Combine(BinariesPath, "Mac", "ld64.lld"));
			}
		}
	}
}
