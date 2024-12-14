// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class libFLAC : ModuleRules
{
	protected virtual string FlacVersion	  { get { return "flac-1.4.3"; } }
	protected virtual string IncRootDirectory { get { return Target.UEThirdPartySourceDirectory; } }
	protected virtual string LibRootDirectory { get { return Target.UEThirdPartySourceDirectory; } }

	protected virtual string FlacIncPath { get { return Path.Combine(IncRootDirectory, "libFLAC", FlacVersion, "include"); } }
	protected virtual string FlacLibPath { get { return Path.Combine(LibRootDirectory, "libFLAC", FlacVersion, "lib"); } }

	public libFLAC(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		if (Target.IsInPlatformGroup(UnrealPlatformGroup.Windows))
		{
			PublicDefinitions.Add("FLAC__NO_DLL=1");
			string LibraryPath = FlacLibPath + "/Win64/Release/";
			PublicAdditionalLibraries.Add(LibraryPath + "FLAC.lib");
			PublicSystemIncludePaths.Add(FlacIncPath);
		}
	}
}
