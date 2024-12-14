// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class uLangCore_VisionOS : uLangCore
{
	public uLangCore_VisionOS(ReadOnlyTargetRules Target) : base(Target)
	{
        PublicDefinitions.Add("ULANG_PLATFORM_EXTENSION=1");
	}
}
