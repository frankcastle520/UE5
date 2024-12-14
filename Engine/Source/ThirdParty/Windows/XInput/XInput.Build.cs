// Copyright Epic Games, Inc. All Rights Reserved.
using EpicGames.Core;
using System;
using System.IO;
using UnrealBuildTool;

public class XInput : ModuleRules
{

	public XInput(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		if (Target.IsInPlatformGroup(UnrealPlatformGroup.Windows))
		{
			// Note, XInput.lib in this folder is patched to remove the exported symbol DllMain.
			// Reason is because ld-lld.exe links things wrong when this symbol is exported. This must have been an oversight at microsoft
			PublicAdditionalLibraries.Add(Path.Combine(Target.WindowsPlatform.DirectXLibDir, "XInput.lib"));
			PublicDelayLoadDLLs.Add("XInput1_4.dll");
		}
	}
}
