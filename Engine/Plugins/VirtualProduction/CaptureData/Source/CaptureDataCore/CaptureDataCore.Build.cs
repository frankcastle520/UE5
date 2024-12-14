// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;
public class CaptureDataCore : ModuleRules
{
	public CaptureDataCore(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{

		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"ImgMedia",
			"CameraCalibrationCore",
			"MeshDescription",
			"StaticMeshDescription",
			"Engine",
			"CaptureDataUtils"
		});
	}
}
