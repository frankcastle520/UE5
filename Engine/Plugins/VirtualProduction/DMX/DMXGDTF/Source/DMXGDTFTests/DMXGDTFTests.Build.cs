// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class DMXGDTFTests : ModuleRules
{
	public DMXGDTFTests(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;			
	
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"DesktopPlatform",
				"DMXGDTF",
				"DMXZip",
				"Projects",
				"Slate",
				"SlateCore",
				"XmlParser"
			}
		);
	}
}
