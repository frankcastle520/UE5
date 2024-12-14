// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ClonerEffectorMeshBuilder : ModuleRules
{
    public ClonerEffectorMeshBuilder(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core"
            }
        );

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "CoreUObject",
                "DynamicMesh",
                "Engine",
                "GeometryCore",
                "GeometryFramework",
                "GeometryScriptingCore",
                "Niagara",
                "NiagaraCore",
                "ProceduralMeshComponent",
                "StaticMeshDescription"
            }
        );
        
        ShortName = "CEMB";
    }
}