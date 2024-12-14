// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ChaosVisualDebugger : ModuleRules
{
	public ChaosVisualDebugger(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.Add("ChaosVD");

		// LaunchEngineLoop dependencies
		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"ApplicationCore",
				"Core",
				"CoreUObject",
				"Engine",
				"InputCore",
				"InstallBundleManager",
				"MediaUtils",
				"Messaging",
				"MoviePlayer",
				"MoviePlayerProxy",
				"Projects",
				"PreLoadScreen",
				"PIEPreviewDeviceProfileSelector",
				"RenderCore",
				"RHI",
				"Slate",
				"SlateCore",
				"StandaloneRenderer",
				"TraceLog",
				"ProfileVisualizer",
				"PropertyAccessEditor"
			}
		);

		// LaunchEngineLoop IncludePath dependencies
		PrivateIncludePathModuleNames.AddRange(
			new string[] {
				"Launch",
				"AutomationWorker",
				"AutomationController",
				"AutomationTest",
				"DerivedDataCache",
				"HeadMountedDisplay", 
				"MRMesh", 
				"SlateRHIRenderer", 
				"SlateNullRenderer",
			}
		);

		// LaunchEngineLoop editor dependencies
		PrivateDependencyModuleNames.AddRange(
			new string[] {
				
				"PropertyEditor",
				"DerivedDataCache",
				"ToolWidgets",
				"UnrealEd"
			});

		if (Target.IsInPlatformGroup(UnrealPlatformGroup.Linux))
		{
			PrivateDependencyModuleNames.AddRange(
				new string[] {
					"UnixCommonStartup"
				}
			);
		}

		if (Target.Platform == UnrealTargetPlatform.Win64 && Target.Architecture == UnrealArch.X64)
		{
			PrivateDefinitions.Add("USE_D3D12_REDIST=1");
		}
	}
}
