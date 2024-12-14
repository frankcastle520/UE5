// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ActorModifierCoreBlueprint : ModuleRules
{
    public ActorModifierCoreBlueprint(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
        
        PublicDependencyModuleNames.AddRange(
	        new string[]
	        {
		        "BlueprintGraph",
		        "Core",
		        "CoreUObject",
		        "Engine",
		        "SlateCore"
	        }
        );
        
        PrivateDependencyModuleNames.AddRange(
	        new string[]
	        {
		        "ActorModifierCore"
	        }
        );
    }
}