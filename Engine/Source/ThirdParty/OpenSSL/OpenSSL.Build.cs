// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class OpenSSL : ModuleRules
{
	public OpenSSL(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		string OpenSSLVersion = "1.1.1t";
		string IncOpenSSLPath = Path.Combine(ModuleDirectory, OpenSSLVersion, "include");
		string LibOpenSSLPath = Path.Combine(PlatformModuleDirectory, OpenSSLVersion, "lib");
		string PlatformSubdir = PlatformSubdirectoryName;
		
		string ConfigFolder = (Target.Configuration == UnrealTargetConfiguration.Debug && Target.bDebugBuildsActuallyUseDebugCRT) ? "Debug" : "Release";

		if (Target.IsInPlatformGroup(UnrealPlatformGroup.Apple))
		{
			// all IOS platforms share include dir
			string IncPlatformName = Target.IsInPlatformGroup(UnrealPlatformGroup.IOS) ? "IOS" : PlatformSubdir;
			PublicSystemIncludePaths.Add(Path.Combine(IncOpenSSLPath, IncPlatformName));

			string LibPath = Path.Combine(LibOpenSSLPath, PlatformSubdir);

			bool bIsSimulator = (Target.Platform != UnrealTargetPlatform.Mac) &&
				(Target.Architecture == UnrealArch.IOSSimulator || Target.Architecture == UnrealArch.TVOSSimulator);
			string LibExt = bIsSimulator ? ".sim.a" : ".a";
			PublicAdditionalLibraries.Add(Path.Combine(LibPath, "libssl" + LibExt));
			PublicAdditionalLibraries.Add(Path.Combine(LibPath, "libcrypto" + LibExt));
		}
		else if (Target.Platform.IsInGroup(UnrealPlatformGroup.Windows))
		{
			if (Target.Architecture == UnrealArch.Arm64)
			{
				PlatformSubdir = "WinArm64";
			}
			else
			{
				string VSVersion = "VS" + Target.WindowsPlatform.GetVisualStudioCompilerVersionName();
				PlatformSubdir = Path.Combine("Win64", VSVersion);
			}

			// Add includes
			PublicSystemIncludePaths.Add(Path.Combine(IncOpenSSLPath, PlatformSubdir));

			// Add Libs
			string LibPath = Path.Combine(LibOpenSSLPath, PlatformSubdir, ConfigFolder);

			PublicAdditionalLibraries.Add(Path.Combine(LibPath, "libssl.lib"));
			PublicAdditionalLibraries.Add(Path.Combine(LibPath, "libcrypto.lib"));
			PublicSystemLibraries.Add("crypt32.lib");
		}
		else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Unix))
		{
			string IncludePath = Path.Combine(IncOpenSSLPath, "Unix");
			string LibraryPath = Path.Combine(LibOpenSSLPath, "Unix", Target.Architecture.LinuxName);

			PublicSystemIncludePaths.Add(IncludePath);
			PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, "libssl.a"));
			PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, "libcrypto.a"));
		}
		else if (Target.Platform == UnrealTargetPlatform.Android)
		{
			string IncludePath = Path.Combine(IncOpenSSLPath, "Android");
			string LibraryPath = Path.Combine(LibOpenSSLPath, "Android");

			string[] Architectures = new string[] {
				"ARM64",
				"x86",
				"x64",
			};

			PublicSystemIncludePaths.Add(IncludePath);

			foreach(var Architecture in Architectures)
			{
				PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, Architecture, "libcrypto.a"));
				PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, Architecture, "libssl.a"));
			}
		}
	}
}
