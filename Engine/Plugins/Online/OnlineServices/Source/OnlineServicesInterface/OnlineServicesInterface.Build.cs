// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class OnlineServicesInterface : ModuleRules
{
	public OnlineServicesInterface(ReadOnlyTargetRules Target) : base(Target)
    {
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[] {
				"CoreOnline",
				"OnlineBase"
			}
		);

		// OnlineService cannot depend on Engine!
		PrivateDependencyModuleNames.AddRange(
			new string[] { 
				"Core",
			}
		);

		// For UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_5: Online/OnlineUtilsCommon.h
		PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "..", "OnlineServicesCommon", "Public"));
	}
}
