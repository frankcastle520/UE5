// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using UnrealBuildTool;

public class NNERuntimeRDGOnnxruntimeEditor : ModuleRules
{
	public NNERuntimeRDGOnnxruntimeEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		if (Target.Type != TargetType.Editor && Target.Type != TargetType.Program)
		{
			return;
		}

		PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "Internal"));

		string OrtPlatformRelativePath = Path.Combine("Binaries", "ThirdParty", "OnnxruntimeEditor", Target.Platform.ToString());
		string OrtPlatformPath = Path.Combine(PluginDirectory, OrtPlatformRelativePath);
		
		string SharedLibBaseName = "runtimerdg_onnxruntime";

		string SharedLibFileName = "UNSUPPORTED_PLATFORM";

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			SharedLibFileName = SharedLibBaseName + ".dll";

			PublicDelayLoadDLLs.Add(SharedLibFileName);
			
			RuntimeDependencies.Add(Path.Combine(OrtPlatformPath, SharedLibFileName));
		}
		else if (Target.Platform == UnrealTargetPlatform.Linux)
		{
			SharedLibFileName = "lib" + SharedLibBaseName + ".so.1.14.1";
			string SharedLibFileNameWithoutVersion = "lib" + SharedLibBaseName + ".so";
			
			PublicDelayLoadDLLs.Add(Path.Combine(OrtPlatformPath, SharedLibFileNameWithoutVersion));
			PublicDelayLoadDLLs.Add(Path.Combine(OrtPlatformPath, SharedLibFileName));

			RuntimeDependencies.Add(Path.Combine(OrtPlatformPath, SharedLibFileNameWithoutVersion));
			RuntimeDependencies.Add(Path.Combine(OrtPlatformPath, SharedLibFileName));
		}
		else if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			SharedLibFileName = "lib" + SharedLibBaseName + ".1.14.1.dylib";
			string SharedLibFileNameWithoutVersion = "lib" + SharedLibBaseName + ".dylib";

			PublicDelayLoadDLLs.Add(Path.Combine(OrtPlatformPath, SharedLibFileNameWithoutVersion));
			PublicDelayLoadDLLs.Add(Path.Combine(OrtPlatformPath, SharedLibFileName));

			RuntimeDependencies.Add(Path.Combine(OrtPlatformPath, SharedLibFileNameWithoutVersion));
			RuntimeDependencies.Add(Path.Combine(OrtPlatformPath, SharedLibFileName));
		}

		PublicDefinitions.Add("UE_ORT_USE_INLINE_NAMESPACE = 1");
		PublicDefinitions.Add("UE_ORT_INLINE_NAMESPACE_NAME = RuntimeRDG");

		if (Target.Type == TargetType.Game)
		{
			PublicDefinitions.Add("ORT_NO_EXCEPTIONS");
		}

		PublicDefinitions.Add("ORT_API_MANUAL_INIT");
		PublicDefinitions.Add("ONNXRUNTIME_SHAREDLIB_PATH=" + Path.Combine(OrtPlatformRelativePath, SharedLibFileName).Replace('\\', '/'));
	}
}