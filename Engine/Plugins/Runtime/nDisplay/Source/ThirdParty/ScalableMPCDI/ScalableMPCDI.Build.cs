// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class ScalableMPCDI : ModuleRules
{
	public ScalableMPCDI(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		if(Target.Platform == UnrealTargetPlatform.Win64 || Target.Platform == UnrealTargetPlatform.Linux)
		{
			// This is necessary for proper import of MPCDI symbols
			PublicDefinitions.Add("MPCDI_STATIC");

			string IncludePath = Path.Combine(ModuleDirectory, "include");

			// Add MPCDI include paths
			PublicIncludePaths.Add(IncludePath);
			PublicIncludePaths.Add(Path.Combine(IncludePath, "Base"));
			PublicIncludePaths.Add(Path.Combine(IncludePath, "Container"));
			PublicIncludePaths.Add(Path.Combine(IncludePath, "Creators"));
			PublicIncludePaths.Add(Path.Combine(IncludePath, "IO"));
			PublicIncludePaths.Add(Path.Combine(IncludePath, "Utils"));

			// MPCDI library depends on these
			AddEngineThirdPartyPrivateStaticDependencies(Target, "TinyXML2");
			AddEngineThirdPartyPrivateStaticDependencies(Target, "UElibPNG");
			AddEngineThirdPartyPrivateStaticDependencies(Target, "zlib");

			// Pick a proper binary
			if (Target.Platform == UnrealTargetPlatform.Win64)
			{
				PublicAdditionalLibraries.Add(Path.Combine(ModuleDirectory, "lib", "Win64", Target.Architecture.WindowsName, "Release", "mpcdi.lib"));
			}
			else if (Target.Platform == UnrealTargetPlatform.Linux && Target.Architecture == UnrealArch.X64)
			{
				PublicAdditionalLibraries.Add(Path.Combine(ModuleDirectory, "lib", "Linux", Target.Architecture.LinuxName, "Release", "libmpcdi.a"));
			}
		}
	}
}
