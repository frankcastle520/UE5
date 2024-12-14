// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class AvalancheRemoteControlEditor : ModuleRules
{
    public AvalancheRemoteControlEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "AvalancheEditorCore",
                "AvalancheOutliner",
                "AvalancheRemoteControl",
                "Core",
                "CoreUObject",
                "Engine",
                "InputCore",
                "RemoteControl",
                "RemoteControlComponents",
                "RemoteControlComponentsEditor",
                "RemoteControlUI",
                "Slate",
                "SlateCore",
                "ToolMenus",
                "UnrealEd",
            }
        );

        ShortName = "AvRCEd";
    }
}