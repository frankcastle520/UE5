// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.IO;
using System.Linq;
using System.Data;
using System.Text;
using System.Text.RegularExpressions;
using System.Collections.Generic;
using System.Security.Cryptography;
using EpicGames.Core;
using Gauntlet.Utils;
using AutomationTool;
using UnrealBuildTool;
using static AutomationTool.CommandUtils;

/** IOS General Notes:
 * 
 *	Functionality:
 *	
 *		Tooling for iOS automation is in a tumultuous state.
 *		Historically, ios-deploy was used to manage all aspects of automation.
 *		However, recent changes in Apple's system framework have made ios-deploy obsolete for launching apps for iOS versions 17+ (See for details: https://github.com/ios-control/ios-deploy/issues/588)
 *		Alternative tools offer most - but not all - of the features needed to install, launch, monitor, and debug an app.
 *		Because of this, Gauntlet iOS automation is restricted to running on a Mac host and mixes and matches which tools are used for a given operation.
 *		
 *		For now, TargetDeviceIOS uses the following tools to support automation:
 *			ios-deploy			- Third party CLI tool, still used for legacy operations and copying files to/from device - https://github.com/ios-control/ios-deploy
 *			devicectl			- Apple's core device framework. Has issues with copying files FROM the device, but handles just about everything else - run 'xcrun devicectl' for details
 *			libimobiledevice	- Cross-platform third party library, only used for ease of bulk content copies - https://libimobiledevice.org/
 *			
 *		In general:
 *			IF you are using xcode versions 15 or below AND iOS version 16 or below, ios-deploy will be primarily used
 *			IF you are using xcode version 16+, devicectl will be primarily used
 *			IF you are using trying to test on iOS versions 17+, you MUST upgrade to XCode 16 which contains critical app monitoring features. (See https://forums.developer.apple.com/forums/thread/756393)
			
		*** IMPORTANT ****
		Testing iOS 17+ and iOS 16 or lower in a single pass is not supported. Doing so can result in undefined behavior.
		If you need to do this, separate the testing into separate passes targeting each device.
 *			
 *	Other:
 *	
 *		- Builds (including local developer builds used with -dev) must already be signed. If built with UBT, signing is already part of the process. Automated re-signing may be added in the future.
 *		- Installing builds requires your device to be added to a mobile provision, and for your build to have been signed with an embedded mobile provision containing the device (See https://developer.apple.com/documentation/xcode/distributing-your-app-to-registered-devices)
 *		- If your app installs content after launch, consider using a bulk build to prevent timeouts. This also lets you more rapidly test -dev apps because code and content are separated
 *		- If your app requires additional permissions, consider using MDM profiles to prevent permission pop-ups from blocking your tests
 */

namespace Gauntlet
{
	/// <summary>
	/// iOS implementation of a device to run applications
	/// </summary>
	public class TargetDeviceIOS : ITargetDevice
	{
		public class ConnectedDevice
		{
			public string UUID;
			public int IOSMajorVersion;
			public bool bConnected;
			public ITargetDevice AssignedDevice;
			public bool IsAssigned => AssignedDevice != null;
			public ConnectedDevice(string UUID, int IOSMajorVersion, bool bConnected)
			{
				this.UUID = UUID;
				this.IOSMajorVersion = IOSMajorVersion;
				this.bConnected = bConnected;
			}		
		}

		/// <summary>
		/// Whether or not devicectl is used to run most device operations
		/// ios-deploy will be used when this is false.
		/// This property is set based on the installed version of xcode and the ios version of the device
		/// </summary>
		private static bool UseDeviceCtl;

		/// <summary>
		/// Collection of devices visible to the UAT host
		/// </summary>
		private static Dictionary<string, ConnectedDevice> ConnectedDevices;

		/// <summary>
		/// Currently active version of XCode. This can be changed between installed versions using 'xcodes select'
		/// </summary>
		private static int XCodeMajorVersion;

		/// <summary>
		/// Creating an iOS device requires several queries to be performed first.
		/// These results are cached and gated from being refreshed behind this bool
		/// </summary>
		private static bool bStaticInitialized;

		/// <summary>
		/// Prevents separate start threads from managing connected device management at the same time (bulk only)
		/// </summary>
		private static object InitializerMutex = new object();

		/// <summary>
		/// Prevents separate start threads from writing to the network build cache directory at the same time(bulk only)
		/// </summary>
		private static object CacheMutex = new object();

		/// <summary>
		/// Used to prevent re-copies of an entire network share build in the event of a device failure (bulk only)
		/// </summary>
		private static bool HasCopiedNetworkBuild = false;

		#region ITargetDevice
		public string Name { get; protected set; }

		public UnrealTargetPlatform? Platform => UnrealTargetPlatform.IOS;

		public ERunOptions RunOptions { get; set; }

		public bool IsConnected => ConnectedDevices.ContainsKey(UUID) && ConnectedDevices[UUID].bConnected;

		public bool IsOn => true;

		public bool IsAvailable => true;
		#endregion

		[AutoParam(60 * 60 * 2)] // iOS installs can be slow... 2 hour default
		public int MaxInstallTime { get; protected set; }

		/// <summary>
		/// Temp path we use to push/pull things from the device
		/// </summary>
		public string LocalCachePath { get; protected set; }

		/// <summary>
		/// Low-level device name (uuid)
		/// </summary>
		public string UUID { get; protected set; }

		protected Dictionary<EIntendedBaseCopyDirectory, string> LocalDirectoryMappings { get; set; } = new();

		private bool IsDefaultDevice = false;

		private IOSAppInstall InstallCache = null;

		public TargetDeviceIOS(string InName, string InCachePath = null)
		{
			AutoParam.ApplyParamsAndDefaults(this, Globals.Params.AllArguments);

			if (BuildHostPlatform.Current.Platform != UnrealTargetPlatform.Mac)
			{
				throw new AutomationException("Creation of an iOS target device is only supported on Mac at this time.");
			}

			InitializeDevices();

			IsDefaultDevice = string.IsNullOrEmpty(InName) || InName.Equals("default", StringComparison.OrdinalIgnoreCase);
			Name = IsDefaultDevice ? "default" : InName;

			if (IsDefaultDevice)
			{
				IEnumerable<ConnectedDevice> AvailableDevices = ConnectedDevices.Values.Where(Device => !Device.IsAssigned);
				if(!AvailableDevices.Any())
				{
					throw new AutomationException("Ran out of unassigned default devices. Ensure all devices are properly connected to the host.");
				}

				if(!AvailableDevices.Where(Device => Device.bConnected).Any())
				{
					// If some devices are detected, but none connected, attempt connection
					UUID = AvailableDevices.First().UUID;
					Log.Info("Attempting to connect to {UUID}", UUID);
					if (!Connect())
					{
						throw new AutomationException("Failed to connect to {DeviceName}. Ensure the device is properly connected to the host.", UUID);
					}
				}
				else
				{
					// Just select the first available device
					UUID = AvailableDevices.Where(Device => Device.bConnected).First().UUID;
				}

				Log.Verbose("Selected device {DeviceName} as default device", UUID);
			}
			else
			{
				UUID = Name;
				if (!ConnectedDevices.ContainsKey(Name))
				{
					throw new AutomationException("Device with UUID {0} not found in device list", UUID);
				}
				if (!ConnectedDevices[UUID].bConnected)
				{
					Log.Info("Attempting to connect to {DeviceName}");
					if(!Connect())
					{
						throw new AutomationException("Failed to connect to {DeviceName}", UUID);
					}
				}
			}

			ConnectedDevices[UUID].AssignedDevice = this;

			if(XCodeMajorVersion < 15)
			{
				UseDeviceCtl = false;
			}
			else if(XCodeMajorVersion == 15)
			{
				if(ConnectedDevices[UUID].IOSMajorVersion < 17)
				{
					UseDeviceCtl = false;
				}
				else
				{
					string Message = "As of XCode 15, Apple has removed DeveloperDiskImages for iOS 17 and beyond. " +
						"Because of this, ios-deploy cannot be used to run and montior apps within Gauntlet. " +
						"Instead, DeviceCtl replaces this functionality which is fully available as of XCode 16. " +
						"Update to XCode 16 to prevent this error.";
					throw new AutomationException(Message);
				}
			}
			else
			{
				UseDeviceCtl = true;
			}

			LocalCachePath = InCachePath ?? Path.Combine(Globals.TempDir, "IOSDevice_" + UUID);
		}

		#region IDisposable Support
		private bool DisposedValue = false; // To detect redundant calls
		~TargetDeviceIOS()
		{
			Dispose(false);
		}

		protected virtual void Dispose(bool disposing)
		{
			if (!DisposedValue)
			{
				// TODO: dispose managed state (managed objects).
				DisposedValue = true;
			}
		}

		// This code added to correctly implement the disposable pattern.
		public void Dispose()
		{
			// Do not change this code. Put cleanup code in Dispose(bool disposing) above.
			Dispose(true);
			GC.SuppressFinalize(this);
		}
		#endregion

		#region ITargetDevice
		public bool Connect()
		{
			if(UseDeviceCtl)
			{
				if(ConnectedDevices.ContainsKey(UUID) && ConnectedDevices[UUID].bConnected)
				{
					return true; // already connected
				}

				IProcessResult ConnectResult = ExecuteDevicectlCommand("manage pair");
				if(ConnectResult.ExitCode != 0)
				{
					Log.Info("Connecting to {DeviceName} exited with code {ExitCode}:\n{Output}", UUID, ConnectResult.ExitCode, ConnectResult.Output);
					return false;
				}

				string DetailsFile = Path.GetTempFileName();
				string DetailsCommand = string.Format("device info details -j {0}", DetailsFile);
				IProcessResult DetailResult = ExecuteDevicectlCommand(DetailsCommand);
				if(DetailResult.ExitCode != 0)
				{
					Log.Info("Detailing {DeviceName} exited with code {ExitCode}. Skipping connection:\n{Output}", UUID, DetailResult.ExitCode, ConnectResult.Output);
				}

				JsonObject JsonDevice = JsonObject.Parse(File.ReadAllText(DetailsFile)).GetObjectField("result");

				JsonObject ConnectionProperties = JsonDevice
					.GetObjectField("connectionProperties");
				string PairStatus = ConnectionProperties.GetStringField("pairingState");

				if(!PairStatus.Equals("paired", StringComparison.OrdinalIgnoreCase))
				{
					Log.Info("{DeviceName}'s pair status is still not paired. Skipping connection.", UUID);
					return false;
				}

				if(ConnectedDevices.ContainsKey(UUID))
				{
					ConnectedDevices[UUID].bConnected = true;
				}
				else
				{
					string OSVersionString = JsonDevice.GetObjectField("deviceProperties").GetStringField("osVersionNumber");
					int IOSMajorVersion = int.Parse(OSVersionString.Substring(0, OSVersionString.IndexOf('.')));

					ConnectedDevice Device = new ConnectedDevice(UUID, IOSMajorVersion, true);
					ConnectedDevices.Add(UUID, Device);
				}
			}

			return true;
		}

		public bool Disconnect(bool bForce = false)
		{
			// Disconnecting from an iOS device causes a lot of problems for device farm automation
			// Namely, re-pairing a device requires someone to re-trust the host PC manually
			return true;
		}

		public bool PowerOn()
		{
			return true;
		}

		public bool PowerOff()
		{
			return true;
		}

		public bool Reboot()
		{
			// Rebooting tends to lock the device and break connections requiring manual intervention to unlock the device
			return true; 
		}

		public Dictionary<EIntendedBaseCopyDirectory, string> GetPlatformDirectoryMappings()
		{
			return LocalDirectoryMappings;
		}
		public override string ToString()
		{
			return Name;
		}
		#endregion

		public void FullClean()
		{
			string AppOutput;
			if (UseDeviceCtl)
			{
				IProcessResult AppListResult = ExecuteDevicectlCommand("device info apps");
				if (AppListResult.ExitCode != 0)
				{
					throw new AutomationException("App list exited with code {0}: {1}", AppListResult.ExitCode, AppListResult.Output);
				}

				AppOutput = AppListResult.Output;
			}
			else
			{
				IProcessResult AppListResult = ExecuteIOSDeployCommand("-B");
				if (AppListResult.ExitCode != 0)
				{
					throw new AutomationException("App list exited with code {0}: {1}", AppListResult.ExitCode, AppListResult.Output);
				}

				AppOutput = AppListResult.Output;
			}

			Regex AppRegex = new Regex("com[^\\s]*"); // Ex. com.epicgames.enginetest
			IEnumerable<string> AppList = AppRegex.Matches(AppOutput)
				.Where(Match => Match.Success)
				.Select(Match => Match.Value)
				.Where(Match => !Match.Contains("com.apple.", StringComparison.OrdinalIgnoreCase));

			foreach (string App in AppList)
			{
				Log.Info("Uninstalling {App}...", App);

				IProcessResult UninstallResult;
				if(UseDeviceCtl)
				{
					UninstallResult = ExecuteDevicectlCommand(string.Format("device uninstall app {0}", App));
				}
				else
				{
					UninstallResult = ExecuteIOSDeployCommand(string.Format("--bundle_id {0} --uninstall_only", App));
				}

				UninstallResult.WaitForExit();
				if (UninstallResult.ExitCode != 0)
				{
					throw new AutomationException("Uninstalling {0} exited with code {1}: {2}", App, UninstallResult.ExitCode, UninstallResult.Output);
				}
			}
		}

		public void CleanArtifacts()
		{
			if(InstallCache == null)
			{
				throw new AutomationException("Could not deduce Bundle ID and project name due to invalid installation cache. " +
					"Either specify the Bundle ID and Project Name using this function's overload OR ensure CreateAppInstall has been called first.");
			}

			CleanArtifacts(InstallCache.PackageName, InstallCache.ProjectName);
		}

		public void CleanArtifacts(string BundleID, string ProjectName)
		{
			string EngineSavedDirectory = "/Documents/Engine/Saved";
			string ProjectSavedDirectory = string.Format("/Documents/{0}/Saved", ProjectName);

			string CleanEngine = string.Format("--bundle_id {0} --rmtree {1}", BundleID, EngineSavedDirectory);
			string CleanProject = string.Format("--bundle_id {0} --rmtree {1}", BundleID, ProjectSavedDirectory);

			IProcessResult Result = ExecuteIOSDeployCommand(CleanEngine);
			Result.WaitForExit();
			if (Result.ExitCode != 0)
			{
				throw new AutomationException("Cleaning Engine artifacts for {0} exited with code {1}:\n{2}", BundleID, Result.ExitCode, Result.Output);
			}

			Result = ExecuteIOSDeployCommand(CleanProject);
			Result.WaitForExit();
			if (Result.ExitCode != 0)
			{
				throw new AutomationException("Cleaning Project artifacts for {0} exited with code {1}:\n{2}", BundleID, Result.ExitCode, Result.Output);
			}
		}

		public void InstallBuild(UnrealAppConfig AppConfig)
		{
			if(AppConfig.Build is not IOSBuild Build)
			{
				throw new AutomationException("Unsupported build type {0} for iOS!", AppConfig.Build.GetType());
			}

			string BuildPath = Build.SourcePath;
			string AppName = Path.GetFileName(Build.SourcePath);
			if (Globals.IsRunningDev && AppConfig.OverlayExecutable.GetOverlay(BuildPath, out string OverlayExecutable))
			{
				BuildPath = OverlayExecutable;
			}
			else if(Build.IsIPAFile)
			{
				// If the build is an IPA file, unzip it to the cache directory
				string BuildCache = Path.Combine(LocalCachePath, "TempBuildCache");
				string PayloadDirectory = Path.Combine(BuildCache, "Payload");

				if(Directory.Exists(PayloadDirectory))
				{
					Directory.Delete(PayloadDirectory, true);
				}
				Directory.CreateDirectory(BuildCache);

				string IPACommand = string.Format("-x -k {0} {1}", BuildPath, BuildCache);
				if (!AppleBuild.ExecuteIPADittoCommand(IPACommand, out string Output, PayloadDirectory))
				{
					throw new AutomationException("Unable to extract IPA {0}:\n{1}", Build.SourcePath, Output);
				}

				BuildPath = Directory.EnumerateDirectories(PayloadDirectory, "*.app").FirstOrDefault();
				if(BuildPath == null)
				{
					throw new AutomationException("Failed to find app within IPA payload directory!");
				}
			}

			// Uninstall any existing form of the app
			IProcessResult Result;
			if(UseDeviceCtl)
			{
				Result = ExecuteDevicectlCommand(string.Format("device uninstall app {0}", Build.PackageName));
			}
			else
			{
				Result = ExecuteIOSDeployCommand(string.Format("--bundle_id {0} --uninstall_only", Build.PackageName));
			}

			Result.WaitForExit();
			if(Result.ExitCode != 0)
			{
				throw new AutomationException("Uninstall exited with code {0}:\n{1}", Result.ExitCode, Result.Output);
			}

			// Install the app
			// Bad provisions can cause this to spit out errors, ignore these and let higher levels manage the exception's verbosity
			using(ScopedSuspendECErrorParsing ErrorSuspension = new())
			{
				if(UseDeviceCtl)
				{ 
					Result = ExecuteDevicectlCommand(string.Format("device install app {0} -v", BuildPath), MaxInstallTime, AdditionalOptions: ERunOptions.NoStdOutRedirect);
				}
				else
				{
					Result = ExecuteIOSDeployCommand(string.Format("-b \"{0}\"", BuildPath), MaxInstallTime, AdditionalOptions: ERunOptions.NoStdOutRedirect);			
				}
			}

			if (Result.ExitCode != 0)
			{
				throw new AutomationException("Install exited with code {0}:\n{1}", Result.ExitCode, Result.Output);
			}
			
			if(Build.Flags.HasFlag(BuildFlags.Bulk))
			{
				int PayloadIndex = Build.SourcePath.IndexOf("/Payload");
				DirectoryInfo RootBuildDirectory = new(Build.SourcePath.Substring(0, PayloadIndex));
					
				string PackageDirectory = RootBuildDirectory.Parent.FullName;
				string BulkBuildPath = RootBuildDirectory.FullName;
				string BulkContentsPath = Path.Combine(PackageDirectory, "BulkContents");
				if(!Directory.Exists(BulkContentsPath))
				{
					throw new AutomationException("Could not locate a bulk contents directory when attempting to install a bulk build. Is your bulk build configured correctly?");
				}

				// When targeting a build in the network share, pull the build down to the host first for a faster install.
				// Note: it's only faster when the device is connected via USB 3.0, so WiFi is actually faster for our device farm because we use 2.0 for host connections
				// When running on builders, we'll just copy the build directly from the share
				string BuildVolume = GetVolumeName(PackageDirectory);
				string CurrentVolume = GetVolumeName(Environment.CurrentDirectory);
				if(!BuildVolume.Equals(CurrentVolume, StringComparison.OrdinalIgnoreCase) && !IsBuildMachine)
				{
					string BuildCache = Path.Combine(LocalCachePath, "TempBuildCache");
					string RelativeBuildPath = BulkBuildPath.Replace(PackageDirectory, string.Empty).TrimStart('\\').Trim('/');
					string RelativeContentPath = BulkContentsPath.Replace(PackageDirectory, string.Empty).Trim('\\').Trim('/');

					string DestinationBuildPath = Path.Combine(BuildCache, RelativeBuildPath);
					string DestinationContentPath = Path.Combine(BuildCache, RelativeContentPath);
					
					lock(CacheMutex)
					{
						if(!HasCopiedNetworkBuild)
						{
							if (Directory.Exists(BuildCache))
							{
								Directory.Delete(BuildCache, true);
							}
							Directory.CreateDirectory(BuildCache);

							Log.Info("Copying {Source} to {Dest}", BulkBuildPath, DestinationBuildPath);
							SystemHelpers.CopyDirectory(BulkBuildPath, DestinationBuildPath, SystemHelpers.CopyOptions.Mirror);
							Log.Info("Copying {Source} to {Dest}", BulkContentsPath, DestinationContentPath);
							SystemHelpers.CopyDirectory(BulkContentsPath, DestinationContentPath, SystemHelpers.CopyOptions.Mirror);

							Log.Info("Copy to host complete. Starting deploy to {DeviceName}", UUID);

							// If a device has an issue and we need to select a new one, don't re-copy the whole build from the network again!
							HasCopiedNetworkBuild = true;
						}

						BulkBuildPath = DestinationBuildPath;
					}
				}

				string IDeviceFS = Path.Combine(Globals.UnrealRootDir, "Engine", "Extras", "ThirdPartyNotUE", "libimobiledevice", "mac", "idevicefs");
				string BulkContentCopyCommand = string.Format("-u {0} -b {1} -x RequiredCommands.txt", UUID, Build.PackageName);

				if(IsBuildMachine)
				{
					// Required for forcing install via pre-paired network device instead of USB
					BulkContentCopyCommand += " -n";
				}

				Result = ExecuteDeploymentCommand(IDeviceFS, BulkContentCopyCommand, MaxInstallTime, true, BulkBuildPath, ERunOptions.NoStdOutRedirect);

				if(Result.ExitCode != 0)
				{
					throw new AutomationException("Copying bulk content failed with exit code {0}. See above for info", Result.ExitCode);
				}
			}
		}

		public IAppInstall CreateAppInstall(UnrealAppConfig AppConfig)
		{
			if (AppConfig.Build is not IOSBuild Build)
			{
				throw new AutomationException("Unsupported build type {0} for iOS!", AppConfig.Build.GetType());
			}

			string ProjectDirectory = string.Format("/Documents/{0}", AppConfig.ProjectName);
			PopulateDirectoryMappings(ProjectDirectory);

			string AppPath = null;
			if(!UseDeviceCtl)
			{
				if(Globals.IsRunningDev)
				{
					AppConfig.OverlayExecutable.GetOverlay(Build.SourcePath, out AppPath);
				}
				else
				{
					AppPath = Build.SourcePath;
				}
			}

			InstallCache = new IOSAppInstall(AppConfig.Name, this, Build.PackageName, AppConfig.CommandLine, AppConfig.ProjectName, AppPath);
			return InstallCache;
		}

		public void CopyAdditionalFiles(IEnumerable<UnrealFileToCopy> FilesToCopy)
		{
			if(InstallCache == null || string.IsNullOrEmpty(InstallCache.PackageName))
			{
				throw new AutomationException("Could not deduce Bundle ID due to invalid installation cache. " +
					"Either specify the Bundle ID using this function's overload OR ensure CreateAppInstall has been called first.");
			}

			CopyAdditionalFiles(FilesToCopy, InstallCache.PackageName);
		}

		public void CopyAdditionalFiles(IEnumerable<UnrealFileToCopy> FilesToCopy, string BundleID)
		{
			if (FilesToCopy == null || !FilesToCopy.Any())
			{
				return;
			}

			if (!LocalDirectoryMappings.Any())
			{
				throw new AutomationException("Attempted to copy additional files before LocalDirectoryMappings were populated." +
					"{0} must call PopulateDirectoryMappings before attempting to call CopyAdditionalFiles", this.GetType());
			}

			foreach (UnrealFileToCopy FileToCopy in FilesToCopy)
			{
				FileInfo SourceFile = new FileInfo(FileToCopy.SourceFileLocation);
				FileInfo DestinationFile = new FileInfo(string.Format("/{0}/{1}", LocalDirectoryMappings[FileToCopy.TargetBaseDirectory], FileToCopy.TargetRelativeLocation));
				if (!SourceFile.Exists)
				{
					Log.Warning(KnownLogEvents.Gauntlet_DeviceEvent, "File to copy {File} not found. Skipping copy.", FileToCopy);
				}
				SourceFile.IsReadOnly = false;

				Log.Verbose("Copying {Source} to {Destination}", SourceFile, DestinationFile);

				string CopyCommand = string.Format("--bundle_id {0} --upload={1} --to {2}",
					BundleID, SourceFile, DestinationFile);
				IProcessResult Result = ExecuteIOSDeployCommand(CopyCommand, 120);
	
				if(Result.ExitCode != 0)
				{
					if (Result.ExitCode != 0)
					{
						throw new AutomationException("Copy of {0} to {1} exited with code {2}:\n{3}",
							SourceFile, DestinationFile, Result.ExitCode, Result.Output);
					}
				}
			}
		}

		public void PopulateDirectoryMappings(string ProjectDirectory)
		{
			string SavedDirectory = ProjectDirectory + "/Saved";

			LocalDirectoryMappings.Add(EIntendedBaseCopyDirectory.Build, ProjectDirectory + "/Build");
			LocalDirectoryMappings.Add(EIntendedBaseCopyDirectory.Binaries, ProjectDirectory + "/Binaries");
			LocalDirectoryMappings.Add(EIntendedBaseCopyDirectory.Config, SavedDirectory + "/Config");
            LocalDirectoryMappings.Add(EIntendedBaseCopyDirectory.Content, ProjectDirectory + "/Content");
            LocalDirectoryMappings.Add(EIntendedBaseCopyDirectory.Demos, SavedDirectory + "/Demos");
			LocalDirectoryMappings.Add(EIntendedBaseCopyDirectory.PersistentDownloadDir, SavedDirectory + "/PersistentDownloadDir");
			LocalDirectoryMappings.Add(EIntendedBaseCopyDirectory.Profiling, SavedDirectory + "/Profiling");
            LocalDirectoryMappings.Add(EIntendedBaseCopyDirectory.Saved, SavedDirectory);
        }

		public IAppInstance Run(IAppInstall Install)
		{
			if (Install is not IOSAppInstall IOSInstall)
			{
				throw new DeviceException("AppInstance is of incorrect type!");
			}

			// Copy the commandline into a uecommandline.txt
			string CommandLine = IOSInstall.CommandLine.Trim();
			string TempCommandLine = Path.GetTempFileName();
			File.WriteAllText(TempCommandLine, CommandLine);

			IProcessResult CommandLineCopyResult;
			if(UseDeviceCtl)
			{
				string CopyCommand = string.Format("device copy to --device {0} --source {1} --destination {2} --domain-type appDataContainer --domain-identifier {3}", 
					UUID, TempCommandLine, Path.Combine("Documents", "uecommandline.txt"), IOSInstall.PackageName);
				CommandLineCopyResult = ExecuteDevicectlCommand(CopyCommand, UseDeviceID: false);
			}
			else
			{
				string CopyCommand = string.Format("--bundle_id {0} --upload={1} --to {2}",
					IOSInstall.PackageName, TempCommandLine, Path.Combine("/Documents", "uecommandline.txt"));
				CommandLineCopyResult = ExecuteIOSDeployCommand(CopyCommand);
			}

			if(CommandLineCopyResult.ExitCode != 0)
			{
				throw new AutomationException("Failed to copy uecommandline file to device!");
			}

			Log.Info("Launching {0} on {1}", Install.Name, this);
			Log.Verbose(IOSInstall.CommandLine);

			ILongProcessResult AppProcess;
			if(UseDeviceCtl)
			{
				// device process launch	base command
				// --terminate-existing		exit any existing forms of the app
				// --console				pipe process stdout to devicectl, also holds onto the process until exit instead of immediate exit
				// --device					device to run on
				string LaunchCommand = string.Format("device process launch --terminate-existing --console --device {0} {1}", UUID, IOSInstall.PackageName);
				AppProcess = ExecuteDevicectlCommandNoWait(LaunchCommand, AdditionalOptions: ERunOptions.SpewIsVerbose, LocalCache: IOSInstall.Device.LocalCachePath);
			}
			else
			{
				if (IOSInstall.AppPath == null)
				{
					// Sanity check, this should never happen, but it would be tricky to debug without an explicit message if it did!
					throw new AutomationException("The provided install to run has a null app path, this likely means the install was designed " +
						"to run with devicectl instead of ios-deploy.");
				}

				// -m		Skip installation
				// -I		Non-interactive mode, just launch and end launch process when app process ends
				// -b		path to bundle to run
				string LaunchCommand = string.Format("-m -I -b \"{0}\"", IOSInstall.AppPath);
				AppProcess = ExecuteIOSDeployCommandNoWait(LaunchCommand, LocalCache: IOSInstall.Device.LocalCachePath);
			}

			if (AppProcess.HasExited)
			{
				throw new DeviceException("Failed to launch on {0}. {1}", Name, AppProcess.Output);
			}

			return new IOSAppInstance(IOSInstall, AppProcess, IOSInstall.CommandLine);
		}

		public static IProcessResult ExecuteIOSDeployCommand(string CommandLine, string Device, int WaitTime = 60, bool WarnOnTimeout = true, ERunOptions AdditionalOptions = ERunOptions.None)
		{
			string IOSDeploy = Path.Combine(Globals.UnrealRootDir, "Engine/Extras/ThirdPartyNotUE/ios-deploy/bin/ios-deploy");
			if (!string.IsNullOrEmpty(Device))
			{
				CommandLine = string.Format("{0} -i {1}", CommandLine, Device);
			}

			return ExecuteDeploymentCommand(IOSDeploy, CommandLine, WaitTime, WarnOnTimeout, AdditionalOptions: AdditionalOptions);
		}

		public static ILongProcessResult ExecuteIOSDeployCommandNoWait(string CommandLine, ERunOptions AdditionalOptions = ERunOptions.None, string LocalCache = null)
		{
			string IOSDeploy = Path.Combine(Globals.UnrealRootDir, "Engine/Extras/ThirdPartyNotUE/ios-deploy/bin/ios-deploy");
			return ExecuteDeploymentCommandNoWait(IOSDeploy, CommandLine, AdditionalOptions: AdditionalOptions, LocalCache: LocalCache);
		}

		public IProcessResult ExecuteIOSDeployCommand(string CommandLine, int WaitTime = 60, bool WarnOnTimeout = true, bool UseDeviceID = true, ERunOptions AdditionalOptions = ERunOptions.None)
		{
			if (UseDeviceID && !IsDefaultDevice)
			{
				return ExecuteIOSDeployCommand(CommandLine, UUID, WaitTime, WarnOnTimeout, AdditionalOptions);
			}
			else
			{
				return ExecuteIOSDeployCommand(CommandLine, null, WaitTime, WarnOnTimeout, AdditionalOptions);
			}
		}

		public static IProcessResult ExecuteDevicectlCommand(string CommandLine, string Device, int WaitTime = 60, bool WarnOnTimeout = true, ERunOptions AdditionalOptions = ERunOptions.None)
		{
			CommandLine = string.Format("devicectl {0}", CommandLine);

			string XCRun = "/usr/bin/xcrun";
			if (!string.IsNullOrEmpty(Device))
			{
				CommandLine = string.Format("{0} --device {1}", CommandLine, Device);
			}

			return ExecuteDeploymentCommand(XCRun, CommandLine, WaitTime, WarnOnTimeout, AdditionalOptions: AdditionalOptions);
		}

		public static ILongProcessResult ExecuteDevicectlCommandNoWait(string CommandLine, ERunOptions AdditionalOptions = ERunOptions.None, string LocalCache = null)
		{
			CommandLine = string.Format("devicectl {0}", CommandLine);
			string XCRun = "/usr/bin/xcrun";
			return ExecuteDeploymentCommandNoWait(XCRun, CommandLine, AdditionalOptions: AdditionalOptions, LocalCache: LocalCache);
		}

		public IProcessResult ExecuteDevicectlCommand(string CommandLine, int WaitTime = 60, bool WarnOnTimeout = true, bool UseDeviceID = true, ERunOptions AdditionalOptions = ERunOptions.None)
		{
			if(UseDeviceID)
			{
				return ExecuteDevicectlCommand(CommandLine, UUID, WaitTime, WarnOnTimeout, AdditionalOptions);
			}
			else
			{
				return ExecuteDevicectlCommand(CommandLine, null, WaitTime, WarnOnTimeout, AdditionalOptions);
			}
		}

		private static IProcessResult ExecuteDeploymentCommand(string Executable, string CommandLine, int WaitTime = 60, bool WarnOnTimeout = true, string WorkingDir = null, ERunOptions AdditionalOptions = ERunOptions.None)
		{
			if (!AdditionalOptions.HasFlag(ERunOptions.UseShellExecute) && !File.Exists(Executable))
			{
				throw new AutomationException("Unable to find deployment binary at {0}", Executable);
			}

			Log.Info("{DeploymentExecutable} executing '{Command}'", Executable, CommandLine);

			ERunOptions RunOptions = ERunOptions.NoWaitForExit | AdditionalOptions;
			RunOptions |= Log.IsVeryVerbose
				? ERunOptions.AllowSpew
				: ERunOptions.NoLoggingOfRunCommand;


			IProcessResult Result = null;
			try
			{
				Result = CommandUtils.Run(Executable, CommandLine, Options: RunOptions, WorkingDir: WorkingDir);

				if (WaitTime > 0)
				{
					DateTime StartTime = DateTime.Now;

					Result.ProcessObject.WaitForExit(WaitTime * 1000);

					if (Result.HasExited == false)
					{
						if ((DateTime.Now - StartTime).TotalSeconds >= WaitTime)
						{
							string Message = string.Format("{0} timeout after {1} secs: {2}, killing process", Executable, WaitTime, CommandLine);

							if (WarnOnTimeout)
							{
								Log.Warning(Message);
							}
							else
							{
								Log.Info(Message);
							}

							Result.ProcessObject?.Kill();
							Result.ProcessObject?.WaitForExit(15000);
						}
					}
				}
			}
			catch (Exception Ex)
			{
				Log.Warning("Encountered an {ExceptionType} while running device command {Exception}", Ex.GetType().Name, Ex.Message);
			}

			return Result;
		}

		private static ILongProcessResult ExecuteDeploymentCommandNoWait(string Executable, string CommandLine, string WorkingDir = null, ERunOptions AdditionalOptions = ERunOptions.None, string LocalCache = null)
		{
			if (!AdditionalOptions.HasFlag(ERunOptions.UseShellExecute) && !File.Exists(Executable))
			{
				throw new AutomationException("Unable to find deployment binary at {0}", Executable);
			}

			Log.Info("{DeploymentExecutable} executing '{Command}'", Executable, CommandLine);

			ERunOptions RunOptions = ERunOptions.NoWaitForExit | AdditionalOptions;
			RunOptions |= Log.IsVeryVerbose
				? ERunOptions.AllowSpew
				: ERunOptions.NoLoggingOfRunCommand;


			ILongProcessResult Result = null;
			try
			{
				Result = new LongProcessResult(Executable, CommandLine, Options: RunOptions, WorkingDir: WorkingDir, LocalCache: LocalCache);
			}
			catch (Exception Ex)
			{
				Log.Warning("Encountered an {ExceptionType} while running device command {Exception}", Ex.GetType().Name, Ex.Message);
			}

			return Result;
		}

		private static void InitializeDevices()
		{
			lock (InitializerMutex)
			{
				if (bStaticInitialized)
				{
					if (!ConnectedDevices.Any())
					{
						throw new AutomationException("No iOS device connections were detected! Verify the host's setup.");
					}

					return;
				}

				bStaticInitialized = true;

				// Determine which version of XCode is installed
				// On XCode version 15 or greater, devicectl will be used to manage deployments
				// On XCode version 14 or lower, ios-deploy will be used to manage deployments
				IProcessResult VersionResult = CommandUtils.Run("xcodebuild", "-version");
				if (VersionResult.ExitCode != 0)
				{
					throw new AutomationException("XCode version query exited with code {0}. Do you have XCode installed?\n{1}", VersionResult.ExitCode, VersionResult.Output);
				}

				Match VersionMatch = Regex.Match(VersionResult.Output, @"Xcode\ (?<Version>\d+)");
				if (!VersionMatch.Success)
				{
					throw new AutomationException("Failed to match version output, has Apple changed output formatting?");
				}

				XCodeMajorVersion = int.Parse(VersionMatch.Groups[1].Value);

				UseDeviceCtl = XCodeMajorVersion >= 15;

				// Gather the iOS devices are connected to this UAT host
				ConnectedDevices = GetConnectedDevices();

				// Clear any zombie processes previous UAT instances may have left running
				AppleBuild.ExecuteCommand("killall", "xcrun").WaitForExit();
				AppleBuild.ExecuteCommand("killall", "ios-deploy").WaitForExit();
				AppleBuild.ExecuteCommand("killall", "lldb").WaitForExit();

				if (!ConnectedDevices.Any())
				{
					throw new AutomationException("No iOS device connections were detected! Verify the host's setup.");
				}
			}
		}

		private static Dictionary<string, ConnectedDevice> GetConnectedDevices()
		{
			Dictionary<string, ConnectedDevice> Devices = new();

			if (UseDeviceCtl)
			{
				string ListFile = Path.GetTempFileName();
				string ListCommand = string.Format("list devices -j {0}", ListFile);

				IProcessResult ListResult = ExecuteDevicectlCommand(ListCommand, null);
				if (ListResult.ExitCode != 0)
				{
					Log.Error("List devices exited with code {0}:\n{1}", ListResult.ExitCode, ListResult.Output);
					return null;
				}

				if(!File.Exists(ListFile))
				{
					Log.Error("Could not find list device output file at {FilePath}", ListFile);
					return null;
				}

				string Text = File.ReadAllText(ListFile);
				JsonObject[] JsonDevices = JsonObject.Parse(File.ReadAllText(ListFile))
					.GetObjectField("result")
					.GetObjectArrayField("devices");

				foreach (JsonObject JsonDevice in JsonDevices)
				{
					string UUID = JsonDevice.GetObjectField("hardwareProperties").GetStringField("udid");
					string OSVersionString = JsonDevice.GetObjectField("deviceProperties").GetStringField("osVersionNumber");
					int IOSMajorVersion = int.Parse(OSVersionString.Substring(0, OSVersionString.IndexOf('.')));
					bool bIsPaired = JsonDevice.GetObjectField("connectionProperties").GetStringField("pairingState").Equals("paired", StringComparison.OrdinalIgnoreCase);

					ConnectedDevice Device = new ConnectedDevice(UUID, IOSMajorVersion, bIsPaired);
					Devices.Add(UUID, Device);
				}
			}
			else
			{
				IProcessResult DetectResult = ExecuteIOSDeployCommand("--detect", null);
				if (DetectResult.ExitCode != 0)
				{
					Log.Warning("Detect devices exited with code {0}:\n{1}", DetectResult.ExitCode, DetectResult.Output);
					return null;
				}

				IEnumerable<Match> DeviceMatches = Regex
					.Matches(DetectResult.Output, @"(.?)Found\ ([a-z0-9]{40}|[A-Z0-9]{8}-[A-Z0-9]{16})(.*?)(\d+\.)")
					.Where(Match => Match.Success);

				foreach(Match Match in DeviceMatches)
				{
					string UUID = Match.Groups[2].ToString();
					string OSVersionString = Match.Groups[4].ToString();
					int IOSMajorVersion = int.Parse(OSVersionString.Substring(0, OSVersionString.IndexOf('.')));

					ConnectedDevice Device = new ConnectedDevice(UUID, IOSMajorVersion, true);
					Devices.Add(UUID, Device);
				}
			}

			return Devices;
		}

		private static string GetVolumeName(string InPath)
		{
			Match M = Regex.Match(InPath, @"/Volumes/(.+?)/");

			if (M.Success)
			{
				return M.Groups[1].ToString();
			}

			return string.Empty;
		}

		// DEPRECATED
		public IAppInstall InstallApplication(UnrealAppConfig AppConfig)
		{
			InstallBuild(AppConfig);
			return CreateAppInstall(AppConfig);
		}
	}

	class IOSAppInstall : IAppInstall
	{
		public string Name { get; protected set; }

		public string AppPath { get; protected set; }

		public string CommandLine { get; protected set; }

		public string PackageName { get; protected set; }

		public string ProjectName { get; protected set; }

		public ITargetDevice Device => IOSDevice;

		public TargetDeviceIOS IOSDevice;

		public IOSAppInstall(string InName, TargetDeviceIOS InDevice, string InPackageName,
			string InCommandLine, string InProjectName, string InAppPath = null)
		{
			Name = InName;
			AppPath = InAppPath;
			IOSDevice = InDevice;
			CommandLine = InCommandLine;
			PackageName = InPackageName;
			ProjectName = InProjectName;
		}

		public IAppInstance Run()
		{
			return Device.Run(this);
		}
	}

	class IOSAppInstance : IAppInstance
	{
		public ITargetDevice Device => Install.Device;
		public TargetDeviceIOS IOSDevice => Device as TargetDeviceIOS;
		public ILongProcessResult LaunchProcess { get; private set; }
		public string CommandLine { get; private set; }
		public bool WasKilled { get; protected set; }
		public int ExitCode => LaunchProcess.ExitCode;
		public bool HasExited => LaunchProcess.HasExited;

		public string ArtifactPath
		{
			get
			{
				if (!bHaveSavedArtifacts)
				{
					if (HasExited)
					{
						SaveArtifacts();
						bHaveSavedArtifacts = true;
					}
				}

				return Path.Combine(IOSDevice.LocalCachePath, "Saved");
			}
		}

		public string StdOut
		{
			get
			{
				CheckGeneratedCrashLog();
				return LaunchProcess.Output;
			}
		}
		private void CheckGeneratedCrashLog()
		{
			// The ios application is being run under lldb by ios-deploy
			// lldb catches crashes and we have it setup to dump thread callstacks
			// parse any crash dumps into Unreal crash format and append to output
			if (HasExited && !bWasCheckedForCrash)
			{
				bWasCheckedForCrash = true;
				string CrashLog = LLDBCrashParser.GenerateCrashLog(LaunchProcess.GetLogReader());
				if (!string.IsNullOrEmpty(CrashLog))
				{
					LaunchProcess.AppendToOutput(CrashLog, false);
				}
			}
		}

		public ILogStreamReader GetLogReader()
		{
			CheckGeneratedCrashLog();
			return LaunchProcess.GetLogReader();
		}

		public ILogStreamReader GetLogBufferReader() => LaunchProcess.GetLogBufferReader();

		public bool WriteOutputToFile(string FilePath) => LaunchProcess.WriteOutputToFile(FilePath) != null;

		protected IOSAppInstall Install;

		protected bool bHaveSavedArtifacts = false;
		protected bool bWasCheckedForCrash = false;

		public IOSAppInstance(IOSAppInstall InInstall, ILongProcessResult InProcess, string InCommandLine)
		{
			Install = InInstall;
			this.CommandLine = InCommandLine;
			this.LaunchProcess = InProcess;
		}

		public int WaitForExit()
		{
			if (!HasExited)
			{
				LaunchProcess.WaitForExit();
			}

			return ExitCode;
		}

		public void Kill(bool bGenerateDump = false)
		{
			if (!HasExited)
			{
				WasKilled = true;
				LaunchProcess.ProcessObject.Kill(true);
			}
		}

		protected void SaveArtifacts()
		{
			IProcessResult DownloadResult;
			string SourceArtifactPath = IOSDevice.GetPlatformDirectoryMappings()[EIntendedBaseCopyDirectory.Saved];
			string DestinationArtifactPath = Path.Combine(IOSDevice.LocalCachePath, "Saved");

			if (Directory.Exists(DestinationArtifactPath))
			{
				Directory.Delete(DestinationArtifactPath, true);
			}

			// ios-deploy will copy the entire Documents directory, we just want the contents
			// We'll copy the directory to a temp path before moving the contents into the saved directory
			string TempPath = Path.Combine(IOSDevice.LocalCachePath, "Temp");
			if (Directory.Exists(TempPath))
			{
				Directory.Delete(TempPath, true);
			}
			Directory.CreateDirectory(TempPath);

			string CopyCommand = string.Format("--bundle_id {0} --download={1} --to {2}",
				Install.PackageName, SourceArtifactPath, TempPath);
			DownloadResult = IOSDevice.ExecuteIOSDeployCommand(CopyCommand, 120);
			if (DownloadResult.ExitCode != 0)
			{
				string Error = string.Format("Copying artifacts from device failed with code {0}:\n{1}\nArtifacts will not be saved.",
					DownloadResult.ExitCode, DownloadResult.Output);
				Log.Error(KnownLogEvents.Gauntlet_DeviceEvent, Error);
			}

			try
			{
				string DocumentsPath = Path.Combine(TempPath, "Documents");
				if (Directory.Exists(DocumentsPath))
				{
					Directory.Move(DocumentsPath, DestinationArtifactPath);
				}

				Directory.Delete(TempPath, true);
			}
			catch (Exception Ex)
			{
				Log.Error(KnownLogEvents.Gauntlet_DeviceEvent, "Failed to move artifacts out of temp directory:\n{Exception}", Ex.Message);
			}
		}
	}


	/// <summary>
	/// Helper class to parses LLDB crash threads and generate Unreal compatible log callstack
	/// </summary>
	static class LLDBCrashParser
	{
		// Frame in callstack
		class FrameInfo
		{
			public string Module;
			public string Symbol = String.Empty;
			public string Address;
			public string Offset;
			public string Source;
			public string Line;

			public override string ToString()
			{
				// symbolicated
				if (!string.IsNullOrEmpty(Source))
				{
					return string.Format("Error: [Callstack] 0x{0} {1}!{2} [{3}{4}]", Address, Module, Symbol.Replace(" ", "^"), Source, string.IsNullOrEmpty(Line) ? "" : ":" + Line);
				}

				// unsymbolicated
				return string.Format("Error: [Callstack] 0x{0} {1}!{2} [???]", Address, Module, Symbol.Replace(" ", "^"));
			}

		}

		// Parsed thread callstack
		class ThreadInfo
		{
			public int Num;
			public string Status;
			public bool Current;
			public List<FrameInfo> Frames = new List<FrameInfo>();

			public override string ToString()
			{
				return string.Format("{0}{1}{2}\n{3}", Num, string.IsNullOrEmpty(Status) ? "" : " " + Status + " ", Current ? " (Current)" : "", string.Join("\n", Frames));
			}
		}

		/// <summary>
		/// Parse lldb thread crash dump to Unreal log format
		/// </summary>
		public static string GenerateCrashLog(ILogStreamReader LogReader)
		{
			try
			{
				DateTime TimeStamp;
				int Frame;
				ThreadInfo Thread = ParseCallstack(LogReader, out TimeStamp, out Frame);
				if (Thread == null)
				{
					return null;
				}

				StringBuilder CrashLog = new StringBuilder();
				CrashLog.Append(string.Format("[{0}:000][{1}]LogCore: === Fatal Error: ===\n", TimeStamp.ToString("yyyy.mm.dd - H.mm.ss"), Frame));
				CrashLog.Append(string.Format("Error: Thread #{0} {1}\n", Thread.Num, Thread.Status));
				CrashLog.Append(string.Join("\n", Thread.Frames));

				return CrashLog.ToString();
			}
			catch (Exception Ex)
			{
				Log.Warning(KnownLogEvents.Gauntlet_DeviceEvent, "Exception parsing LLDB callstack {Exception}", Ex.Message);
			}

			return null;

		}

		private static ThreadInfo ParseCallstack(ILogStreamReader LogReader, out DateTime Timestamp, out int FrameNum)
		{
			Timestamp = DateTime.UtcNow;
			FrameNum = 0;

			Regex LogLineRegex = new Regex(@"(?<timestamp>\s\[\d.+\]\[\s*\d+\])(?<log>.*)");
			Regex TimeRegex = new Regex(@"\[(?<year>\d+)\.(?<month>\d+)\.(?<day>\d+)-(?<hour>\d+)\.(?<minute>\d+)\.(?<second>\d+):(?<millisecond>\d+)\]\[(?<frame>\s*\d+)\]", RegexOptions.IgnoreCase);
			Regex ThreadRegex = new Regex(@"(thread\s#)(?<threadnum>\d+),?(?<status>.+)");
			Regex SymbolicatedFrameRegex = new Regex(@"\*?\s#(?<framenum>\d+):\s0x(?<address>[\da-f]+)\s(?<module>.+)\`(?<symbol>.+)(\sat\s)(?<source>.+)\s\[opt\]");
			Regex UnsymbolicatedFrameRegex = new Regex(@"\*?frame\s#(?<framenum>\d+):\s0x(?<address>[\da-f]+)\s(?<module>.+)\`(?<symbol>.+)(\s\+\s(?<offset>\d+))?");

			List<ThreadInfo> Threads = new List<ThreadInfo>();
			ThreadInfo Thread = null;

			foreach(string Line in LogReader.EnumerateNextLines())
			{
				// If Gauntlet marks the test as complete, ignore any thread dumps from forcing process to exit
				if (Line.Contains("**** TEST COMPLETE. EXIT CODE: 0 ****"))
				{
					return null;
				}

				// Parse log timestamps
				if (LogLineRegex.IsMatch(Line))
				{
					GroupCollection LogGroups = LogLineRegex.Match(Line).Groups;
					if (TimeRegex.IsMatch(LogGroups["timestamp"].Value))
					{
						GroupCollection TimeGroups = TimeRegex.Match(LogGroups["timestamp"].Value).Groups;
						int Year = int.Parse(TimeGroups["year"].Value);
						int Month = int.Parse(TimeGroups["month"].Value);
						int Day = int.Parse(TimeGroups["day"].Value);
						int Hour = int.Parse(TimeGroups["hour"].Value);
						int Minute = int.Parse(TimeGroups["minute"].Value);
						int Second = int.Parse(TimeGroups["second"].Value);
						FrameNum = int.Parse(TimeGroups["frame"].Value);
						Timestamp = new DateTime(Year, Month, Day, Hour, Minute, Second);
					}

					continue;
				}

				if (Thread != null)
				{
					FrameInfo Frame = null;
					GroupCollection FrameGroups = null;

					// Parse symbolicated frame
					if (SymbolicatedFrameRegex.IsMatch(Line))
					{
						FrameGroups = SymbolicatedFrameRegex.Match(Line).Groups;

						Frame = new FrameInfo()
						{
							Address = FrameGroups["address"].Value,
							Module = FrameGroups["module"].Value,
							Symbol = FrameGroups["symbol"].Value,
						};

						Frame.Source = FrameGroups["source"].Value;
						if (Frame.Source.Contains(":"))
						{
							Frame.Source = FrameGroups["source"].Value.Split(':')[0];
							Frame.Line = FrameGroups["source"].Value.Split(':')[1];
						}
					}

					// Parse unsymbolicated frame
					if (UnsymbolicatedFrameRegex.IsMatch(Line))
					{
						FrameGroups = UnsymbolicatedFrameRegex.Match(Line).Groups;

						Frame = new FrameInfo()
						{
							Address = FrameGroups["address"].Value,
							Offset = FrameGroups["offset"].Value,
							Module = FrameGroups["module"].Value,
							Symbol = FrameGroups["symbol"].Value
						};
					}

					if (Frame != null)
					{
						Thread.Frames.Add(Frame);
					}
					else
					{
						Thread = null;
					}

				}

				// Parse thread
				if (ThreadRegex.IsMatch(Line))
				{

					GroupCollection ThreadGroups = ThreadRegex.Match(Line).Groups;
					int Num = int.Parse(ThreadGroups["threadnum"].Value);
					string Status = ThreadGroups["status"].Value.Trim();

					Thread = Threads.SingleOrDefault(T => T.Num == Num);

					if (Thread == null)
					{
						Thread = new ThreadInfo()
						{
							Num = Num,
							Status = Status
						};

						if (Line.Trim().StartsWith("*"))
						{
							Thread.Current = true;
						}

						Threads.Add(Thread);

					}
				}
			}

			if (Threads.Count(T => T.Current == true) > 1)
			{
				Log.Warning(KnownLogEvents.Gauntlet_DeviceEvent, "LLDB debug parsed more than one current thread");
			}

			Thread = Threads.FirstOrDefault(T => T.Current == true);

			if (Threads.Count > 0 && Thread == null)
			{
				Log.Warning(KnownLogEvents.Gauntlet_DeviceEvent, "Unable to parse full crash callstack");
			}


			// Do not want to surface crashes which happen as a result of requesting exit
			if (Thread != null && Thread.Frames.FirstOrDefault(F => F.Symbol.Contains("::RequestExit")) != null)
			{
				Thread = null;
			}

			return Thread;
		}
	}

	public class IOSDeviceFactory : IDeviceFactory
	{
		public bool CanSupportPlatform(UnrealTargetPlatform? Platform)
		{
			return Platform == UnrealTargetPlatform.IOS;
		}

		public ITargetDevice CreateDevice(string InRef, string InCachePath, string InParam)
		{
			return new TargetDeviceIOS(InRef, InCachePath);
		}
	}

	public class IOSBuildSupport : BaseBuildSupport
	{
		protected override BuildFlags SupportedBuildTypes => BuildFlags.Packaged | BuildFlags.CanReplaceCommandLine | BuildFlags.CanReplaceExecutable | BuildFlags.Bulk | BuildFlags.NotBulk;
		protected override UnrealTargetPlatform? Platform => UnrealTargetPlatform.IOS;
	}
}