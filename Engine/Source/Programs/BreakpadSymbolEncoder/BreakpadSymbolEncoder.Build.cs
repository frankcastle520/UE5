// Copyright Epic Games, Inc. All Rights Reserved.

using System.Globalization;
using System.IO;
using UnrealBuildBase;
using UnrealBuildTool;

public class BreakpadSymbolEncoder : ModuleRules
{
	public BreakpadSymbolEncoder(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicIncludePathModuleNames.Add("Launch");

		bAddDefaultIncludePaths = false;
	}
}
