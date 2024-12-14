// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class GuidelinesSupportLibrary : ModuleRules
{
	public GuidelinesSupportLibrary(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		// Modification made by epic to gsl/assert and gsl/pointers.
		//See comments with @epic.
		//Use the 4.0.0 + bug fix (1144) from the Microsoft implementation.
		string GuidelinesSupportLibraryVersionDir = "GSL-1144";
		
		string GuidelinesSupportLibraryPath = Path.Combine(Target.UEThirdPartySourceDirectory, "GuidelinesSupportLibrary", GuidelinesSupportLibraryVersionDir);
		string GuidelinesSupportLibraryIncludePath = Path.Combine(GuidelinesSupportLibraryPath, "include");
		
		PublicSystemIncludePaths.Add(GuidelinesSupportLibraryIncludePath);
		PublicDefinitions.Add("GSL_NO_IOSTREAMS");
	}
}
