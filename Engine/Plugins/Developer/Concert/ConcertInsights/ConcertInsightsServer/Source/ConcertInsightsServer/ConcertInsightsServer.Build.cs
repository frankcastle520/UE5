// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ConcertInsightsServer : ModuleRules
{
    public ConcertInsightsServer(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
            }
        );

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
	            "ConcertInsightsCore", 
	            "ConcertSyncServer", 
                "CoreUObject",
                "Slate",
                "SlateCore",
                "ToolMenus", 
            }
        );
        
        ShortName = "CrtInS";
    }
}