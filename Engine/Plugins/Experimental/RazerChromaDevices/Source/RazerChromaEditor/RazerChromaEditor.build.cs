// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class RazerChromaEditor : ModuleRules
	{
		public RazerChromaEditor(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[] {
					"Core",
					"CoreUObject",
					"MediaAssets",
					"UnrealEd",
					"Engine",
					"RazerChromaDevices",
					"SlateCore",
					"Slate",
					"ToolMenus",
					"Projects"
				});
			
			PrivateDependencyModuleNames.Add("RazerChromaSDK");
			
		}
	}
}