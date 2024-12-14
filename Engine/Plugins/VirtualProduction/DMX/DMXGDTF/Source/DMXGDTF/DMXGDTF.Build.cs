// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class DMXGDTF : ModuleRules
{
	public DMXGDTF(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject"
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"DMXZip",
				"Engine",
				"XmlParser"
			}
		);
	}
}
