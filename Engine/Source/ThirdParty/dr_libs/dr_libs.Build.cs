// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System;
using System.IO;

public class dr_libs : ModuleRules
{

	protected virtual bool bPlatformSupports_dr_libs_MP3
	{
		get
		{
			return (Target.Platform.IsInGroup(UnrealPlatformGroup.Windows) && Target.Architecture != UnrealArch.Arm64) ||
				   (Target.IsInPlatformGroup(UnrealPlatformGroup.Unix) && Target.Architecture != UnrealArch.Arm64) ||
				   Target.Platform == UnrealTargetPlatform.Mac ||
				   Target.Platform == UnrealTargetPlatform.IOS ||
				   // Android: we only have arm64 libs, so we can't enable it when building for x86 or x86+arm64, since there's only one #define possible
				   (Target.Platform == UnrealTargetPlatform.Android && !Target.Architectures.bIsMultiArch && Target.Architecture == UnrealArch.Arm64) ||
					false;
		}
	}

	protected virtual string IncRootDirectory { get { return Target.UEThirdPartySourceDirectory; } }
	protected virtual string LibRootDirectory { get { return Target.UEThirdPartySourceDirectory; } }

	protected virtual string drlibsIncPath { get { return Path.Combine(IncRootDirectory, "dr_libs", "dr_libs", "include"); } }
	protected virtual string drlibsLibPath { get { return Path.Combine(LibRootDirectory, "dr_libs", "dr_libs", "lib"); } }

	public dr_libs(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		PublicDefinitions.Add(String.Format("WITH_DR_LIBS_MP3={0}", bPlatformSupports_dr_libs_MP3 ? 1 : 0));
		if (bPlatformSupports_dr_libs_MP3)
		{
			PublicSystemIncludePaths.Add(drlibsIncPath);

		    if (Target.IsInPlatformGroup(UnrealPlatformGroup.Windows))
		    {
			    string LibraryPath = Path.Combine(drlibsLibPath, "Win64");
			    PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, "libdr_libs_mp3-Win64-Shipping.lib"));
		    }
		    else if (Target.Platform == UnrealTargetPlatform.Android)
		    {
			    string LibraryPath = Path.Combine(drlibsLibPath, "Android", "arm64-v8a");
			    PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, "libdr_libs_mp3-Android-Shipping.a"));
		    }
		    else if (Target.Platform == UnrealTargetPlatform.Mac)
		    {
			    string LibraryPath = Path.Combine(drlibsLibPath, "Mac");
			    PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, "libdr_libs_mp3-Mac-Shipping.a"));
		    }
		    else if (Target.Platform == UnrealTargetPlatform.IOS)
		    {
			    string LibraryPath = Path.Combine(drlibsLibPath, "IOS");
			    PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, "libdr_libs_mp3-IOS-Shipping.a"));
		    }
		    else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Unix))
            {
			    string LibraryPath = Path.Combine(drlibsLibPath, "Linux");
			    PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, Target.Architecture.LinuxName, "libdr_libs_mp3-Linux-Shipping.a"));
            }
        }
	}
}
