// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class MarketplaceKit : ModuleRules
{
	public MarketplaceKit(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.Add("Swift");

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"Engine",
		});

		PublicAdditionalLibraries.Add(Path.Combine(ModuleDirectory, "arm64", "libMarketplaceKitWrapper.a"));
	}
}
