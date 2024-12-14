// Copyright Epic Games, Inc. All Rights Reserved.

using AutomationTool;
using System;
using System.IO;
using UnrealBuildTool;

namespace Gauntlet
{
	/// <summary>
	/// Manages the overlaying of a development executable over an existing build.
	/// These are useful for testing code changes without needing to re-cook or re-package builds.
	/// To use overlay executables, simply specify -dev on the UAT commandline.
	/// The overlay executable will automatically be applied as long as the following conditions are met
	///		- The build being used supports Overlay executables. See BuildFlags.CanReplaceExecutable
	///		- The development executable's write time is newer than that of the supplied builds write time
	/// </summary>
	public class OverlayExecutable
	{
		private string ProjectName;
		private string Flavor;
		private UnrealTargetPlatform Platform;
		private UnrealTargetConfiguration Configuration;
		private UnrealTargetRole Role;

		public OverlayExecutable(UnrealSessionRole SessionRole, string ProjectName)
		{
			this.ProjectName = ProjectName;
			this.Configuration = SessionRole.Configuration;
			this.Role = SessionRole.RoleType;
			this.Platform = SessionRole.Platform.Value;
			this.Flavor = SessionRole.RequiredFlavor;
		}

		/// <summary>
		/// Attempts to get the path to a local executable if it is newer than the base executable
		/// </summary>
		/// <param name="BaseExecutable">The base executable to use if an overlay does not exist</param>
		/// <param name="OverlayExecutable"></param>
		/// <param name="ExtensionOverride">What extension the overlay file should have</param>
		/// <returns>True if a local, newer executable that matches the role's requirements exists</returns>
		/// <exception cref="AutomationException"></exception>
		public bool GetOverlay(string BaseExecutable, out string OverlayExecutable, string ExtensionOverride = null)
		{
			OverlayExecutable = null;

			// If no base executable is specified we'll skip the "newer" check and just return a local binary that matches the role
			if (string.IsNullOrEmpty(BaseExecutable))
			{
				throw new AutomationException("Overlay Error: No base executable was specified.");
			}

			if (!File.Exists(BaseExecutable) && !Directory.Exists(BaseExecutable)) // mac/ios apps can be directories
			{
				throw new AutomationException("Overlay Error: Could not find base executable {0}.", BaseExecutable);
			}

			// Ex. /UnrealGame/Binaries/Win64/UnrealClient.exe
			string PlatformBinariesDirectory = Path.Combine(Environment.CurrentDirectory, ProjectName, "Binaries", Platform.ToString());
			string ExecutableName = UnrealHelpers.GetExecutableName(ProjectName, Platform, Configuration, Role, Flavor, string.Empty);
			string ExecutableExtension = string.IsNullOrEmpty(ExtensionOverride) ? Path.GetExtension(BaseExecutable) : ExtensionOverride;
			string LocalExecutable = Path.Combine(PlatformBinariesDirectory, ExecutableName + ExecutableExtension);

			if (!File.Exists(LocalExecutable) && !Directory.Exists(LocalExecutable))
			{
				Log.Verbose("No local executable for {Platform} exists. Skipping overlay for this role", Platform);
				return false;
			}

			if (File.GetLastWriteTime(BaseExecutable) > File.GetLastWriteTime(LocalExecutable))
			{
				Log.Verbose("Local executable for {Platform} is not newer than base executable. Skipping overlay for this role", Platform);
				return false;
			}

			Log.Info("Applying newer local executable as overlay {LocalExecutable}", LocalExecutable);

			OverlayExecutable = LocalExecutable;
			return true;
		}
	}
}