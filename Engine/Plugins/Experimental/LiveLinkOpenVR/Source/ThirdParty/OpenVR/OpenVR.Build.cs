// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.IO;
using UnrealBuildTool;

public class OpenVR : ModuleRules
{
	public OpenVR(ReadOnlyTargetRules Target) : base(Target)
	{
		/** Mark the current version of the OpenVR SDK */
		string OpenVRVersion = "v1_5_17";
		Type = ModuleType.External;

		string SdkBase = Path.Combine(ModuleDirectory, "OpenVR" + OpenVRVersion);
		if (!Directory.Exists(SdkBase))
		{
			string Err = string.Format("OpenVR SDK not found in {0}", SdkBase);
			Console.WriteLine(Err);
			throw new BuildException(Err);
		}

		string BinariesPath = Path.Combine(SdkBase, "bin");
		string HeadersPath = Path.Combine(SdkBase, "headers");
		string LibrariesPath = Path.Combine(SdkBase, "lib");

		PublicSystemIncludePaths.Add(HeadersPath);

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			BinariesPath = Path.Combine(BinariesPath, "win64");
			LibrariesPath = Path.Combine(LibrariesPath, "win64");

			PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "openvr_api.lib"));
			PublicDelayLoadDLLs.Add("openvr_api.dll");
			RuntimeDependencies.Add(Path.Combine(BinariesPath, "openvr_api.dll"));
		}
		else if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			string DylibPath = Path.Combine(BinariesPath, "osx32", "libopenvr_api.dylib");
			PublicDelayLoadDLLs.Add(DylibPath);
			RuntimeDependencies.Add(DylibPath);
		}
		else if (Target.Platform == UnrealTargetPlatform.Linux && Target.Architecture == UnrealArch.X64)
		{
			LibrariesPath = Path.Combine(LibrariesPath, "linux64");
			PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "libopenvr_api.so"));

			string DylibDir = Path.Combine(BinariesPath, "linux64");
			PrivateRuntimeLibraryPaths.Add(DylibDir);

			string DylibPath = Path.Combine(DylibDir, "libopenvr_api.so");
			PublicDelayLoadDLLs.Add(DylibPath);
			RuntimeDependencies.Add(DylibPath);
		}
	}
}
