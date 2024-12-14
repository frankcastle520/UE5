// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class TinyXML2 : ModuleRules
{
	protected readonly string Version = "9.0.0";
	protected string VersionPath { get => Path.Combine(ModuleDirectory, Version); }
	protected string IncludePath { get => Path.Combine(VersionPath, "include"); }
	protected string LibraryPath { get => Path.Combine(VersionPath, "lib"); }

	public TinyXML2(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		PublicSystemIncludePaths.Add(IncludePath);

		if (Target.Platform.IsInGroup(UnrealPlatformGroup.Windows))
		{
			if (Target.WindowsPlatform.Architecture == UnrealArch.Arm64)
			{
				PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, "Win64", Target.Architecture.WindowsLibDir, "Release", "tinyxml2.lib"));
			}
			else
			{
				PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, "Win64", "Release", "tinyxml2.lib"));
			}
		}
		else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Unix))
		{
			PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, "Unix", Target.Architecture.LinuxName, "Release", "libtinyxml2.a"));
		}
	}
}
