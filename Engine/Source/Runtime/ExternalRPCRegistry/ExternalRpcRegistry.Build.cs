// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ExternalRpcRegistry : ModuleRules
{
    public ExternalRpcRegistry(ReadOnlyTargetRules Target) : base(Target)
    {
        PrivateDependencyModuleNames.AddRange(
            new string[] {
                "Core",
                "CoreUObject",
				"Json",
				"HTTP",
				"HTTPServer"
            }
        );
		PublicDefinitions.Add("USE_RPC_REGISTRY_IN_SHIPPING=0");
    }
}
