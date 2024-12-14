// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using UnrealBuildTool;

namespace Gauntlet
{
	public class Win64BuildSource : StagedBuildSource<StagedBuild>
	{
		public override string BuildName { get { return "Win64StagedBuild"; } }

		public override UnrealTargetPlatform Platform { get { return UnrealTargetPlatform.Win64; } }

		public override string PlatformFolderPrefix { get { return "Windows"; } }
	}

	public interface IWindowsSelfInstallingBuild
	{
		void Install(UnrealAppConfig AppConfiguration);
		WindowsAppInstall CreateAppInstall(TargetDeviceWindows TargetDevice, UnrealAppConfig AppConfig, out string BasePath);
	}
}
