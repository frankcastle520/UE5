// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.IO;
using UnrealBuildTool;

public class libpas : ModuleRules
{
	public libpas(ReadOnlyTargetRules Target) : base(Target)
	{
		if (!Target.bUseVerseVM)
		{
			BinariesSubFolder = "NotForLicensees";
		}

		// Disable static analysis for now.
		bDisableStaticAnalysis = true;

		IWYUSupport = IWYUSupport.None;

		// Relative to the Engine/Source directory.
		string libpasDirectory = "ThirdParty/libpas";

		if (Target.Platform == UnrealTargetPlatform.Win64 && !Target.WindowsPlatform.Compiler.IsClang())
		{
			// Instead of compiling libpas on Windows, just use binaries that were prebuilt with clang-cl.
			Type = ModuleType.External;

            string libpasConfigName;
            switch (Target.Configuration)
            {
                case UnrealTargetConfiguration.Debug:
                    libpasConfigName = "DebugUE";
                    break;
                default:
                    libpasConfigName = "ReleaseUE";
                    break;
            }

			string ArchName = Target.Architecture == UnrealArch.Arm64 ? "ARM64" : "x64";
			PublicAdditionalLibraries.Add(Path.Combine(libpasDirectory, ArchName, libpasConfigName, "libpas.lib"));

			// libpas will always be statically linked on Windows.
			PublicDefinitions.Add("LIBPAS_API=");
		}

		// libpas has its own PAS_API define instead of LIBPAS_API.
		PublicDefinitions.Add("PAS_API=LIBPAS_API");
		PublicDefinitions.Add("PAS_BAPI=LIBPAS_API");

		// Let libpas know it's being built as part of UE.
		PublicDefinitions.Add("PAS_UE=1");

		// UE only sees the libpas includes in the ue_include subdirectory.
		PublicSystemIncludePaths.Add(Path.Combine(libpasDirectory, "src", "libpas", "ue_include"));

		bDisableAutoRTFMInstrumentation = true;
	}
}
