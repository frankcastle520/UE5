// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class AvalancheTransitionEditor : ModuleRules
{
    public AvalancheTransitionEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "AvalancheTag",
                "Core",
                "CoreUObject",
                "StateTreeEditorModule",
            }
        );

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "ApplicationCore",
                "AssetDefinition",
                "AssetTools",
                "AvalancheCore",
                "AvalancheTransition",
                "DeveloperSettings",
                "EditorStyle",
                "Engine",
                "InputCore",
                "MessageLog",
                "Projects",
                "PropertyEditor",
                "Slate",
                "SlateCore",
                "StateTreeModule",
                "StructUtilsEditor",
                "ToolMenus",
                "UnrealEd",
            }
        );

        // Until we split use of TraceServices and TraceAnalysis in the debugger we enable only on platforms supporting both at the moment
        if (Target.Platform.IsInGroup(UnrealPlatformGroup.Desktop) && (Target.Configuration != UnrealTargetConfiguration.Shipping || Target.bBuildEditor))
        {
            PrivateDefinitions.Add("WITH_STATETREE_DEBUGGER=1");
            PrivateDependencyModuleNames.AddRange(
                new string[]
                {
                    "TraceAnalysis",
                    "TraceLog",
                    "TraceServices",
                }
            );
        }
        else
        {
            PrivateDefinitions.Add("WITH_STATETREE_DEBUGGER=0");
        }
    }
}
