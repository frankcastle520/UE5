// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class TraceTools : ModuleRules
{
	public TraceTools(ReadOnlyTargetRules Target) : base(Target)
	{
		UnsafeTypeCastWarningLevel = WarningLevel.Error;

		PrivateDependencyModuleNames.AddRange(
				new string[] {
					"ApplicationCore",
					"Core",
					"CoreUObject",
					"InputCore",
					"SessionServices",
					"Slate",
					"SlateCore",
					"Sockets",
					"TraceLog",
				}
			);

		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.Add("SharedSettingsWidgets");
		}
	}
}
