// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.IO;
using UnrealBuildTool;

public class DirectML : ModuleRules
{
	public DirectML(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		// Only Win64
		if (Target.Platform != UnrealTargetPlatform.Win64)
		{
			return;
		}

		string PlatformDir = Target.Platform.ToString();
		if (Target.Platform == UnrealTargetPlatform.Win64 && Target.Architecture == UnrealArch.Arm64)
		{
			PlatformDir = "WinArm64";
		}
		string BinDirPath = Path.GetFullPath(Path.Combine(ModuleDirectory, "bin", PlatformDir));

		string LibFileName = "DirectML";
		string LibSubDir = "DML";
		string DllFileName = LibFileName + ".dll";
		string DbgDllFileName = LibFileName + ".Debug.dll";

		PublicSystemIncludePaths.Add(Path.Combine(ModuleDirectory, "include"));
		PublicAdditionalLibraries.Add(Path.Combine(ModuleDirectory, "lib", PlatformDir, LibFileName + ".lib"));
		
		RuntimeDependencies.Add(Path.Combine("$(TargetOutputDir)", LibSubDir, DllFileName), Path.Combine(BinDirPath, DllFileName));
		PublicDelayLoadDLLs.Add(DllFileName);

		PublicDefinitions.Add("DIRECTML_PATH=" + LibSubDir);

		if (Target.Type == TargetType.Editor && Target.Configuration == UnrealTargetConfiguration.Debug)
		{
			RuntimeDependencies.Add(Path.Combine("$(TargetOutputDir)", LibSubDir, DbgDllFileName), Path.Combine(BinDirPath, DbgDllFileName));
			PublicDelayLoadDLLs.Add(DbgDllFileName);
		}
	}
}
