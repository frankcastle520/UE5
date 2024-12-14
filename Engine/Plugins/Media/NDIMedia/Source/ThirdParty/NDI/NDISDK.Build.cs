// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class NDISDK : ModuleRules
{
    public NDISDK(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.External;

		// The NDI SDK is available for Win64 + Linux, but this plugin only supports Win64
        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            string IncludePath = Path.Combine(ModuleDirectory, "Include");
	        PublicIncludePaths.Add(IncludePath);

            string DllName = Path.Combine("Processing.NDI.Lib.x64.dll");
            string DllPath = Path.Combine(PluginDirectory, "Binaries", "ThirdParty", "Win64", DllName);
            RuntimeDependencies.Add(DllPath);

            // Ensure that we define our c++ define
            PublicDefinitions.Add("NDI_SDK_ENABLED");
        }
    }
}
