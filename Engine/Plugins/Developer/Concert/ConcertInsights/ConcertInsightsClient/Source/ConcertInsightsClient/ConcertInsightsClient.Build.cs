// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ConcertInsightsClient : ModuleRules
{
    public ConcertInsightsClient(ReadOnlyTargetRules Target) : base(Target)
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
	            "ConcertSharedSlate",
	            "ConcertSyncClient",
                "CoreUObject",
                "EditorTraceUtilities",
                "Engine",
                "Slate",
                "SlateCore",
                "ToolMenus", 
            }
        );
        
        ShortName = "CrtInEd";
    }
}