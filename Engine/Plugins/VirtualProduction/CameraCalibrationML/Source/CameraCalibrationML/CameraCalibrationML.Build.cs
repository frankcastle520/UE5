// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class CameraCalibrationML : ModuleRules
	{
		public CameraCalibrationML(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"CameraCalibrationEditor",
					"Core",
				}
			);
		}
	}
}
