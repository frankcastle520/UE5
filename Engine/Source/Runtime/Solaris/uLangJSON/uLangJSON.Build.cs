// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class uLangJSON : ModuleRules
{
	public uLangJSON(ReadOnlyTargetRules Target) : base(Target)
	{
		IWYUSupport = IWYUSupport.None;

		PCHUsage = PCHUsageMode.NoPCHs;
		bRequiresImplementModule = false;
		BinariesSubFolder = "NotForLicensees";

		PublicDependencyModuleNames.AddRange(
			new string[] {
				"RapidJSON",
				"uLangCore",
			}
		);

		bDisableAutoRTFMInstrumentation = true;
	}
}
