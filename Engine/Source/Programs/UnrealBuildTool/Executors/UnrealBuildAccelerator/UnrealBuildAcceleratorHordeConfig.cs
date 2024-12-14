// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using EpicGames.Core;
using EpicGames.Horde.Compute;

namespace UnrealBuildTool
{
	/// <summary>
	/// Configuration for Unreal Build Accelerator Horde session
	/// </summary>
	[System.Diagnostics.CodeAnalysis.SuppressMessage("Style", "IDE1006:Naming Styles", Justification = "UnrealBuildTool naming style")]
	class UnrealBuildAcceleratorHordeConfig
	{
		/// <summary>
		/// Uri of the Horde server
		/// </summary>
		[ConfigFile(ConfigHierarchyType.Engine, "Horde", "ServerUrl")]
		[XmlConfigFile(Category = "Horde", Name = "Server")]
		[CommandLine("-UBAHorde=")]
		public string? HordeServer { get; set; }

		/// <summary>
		/// Auth token for the Horde server
		/// </summary>
		[ConfigFile(ConfigHierarchyType.Engine, "Horde", "UbaToken")]
		[XmlConfigFile(Category = "Horde", Name = "Token")]
		[CommandLine("-UBAHordeToken=")]
		public string? HordeToken { get; set; }

		/// <summary>
		/// OIDC id for the login to use
		/// </summary>
		[Obsolete("The OidcProvider option is no longer used")]
		[XmlConfigFile(Category = "Horde", Name = "OidcProvider")]
		[CommandLine("-UBAHordeOidc=")]
		public string? HordeOidcProvider { get; set; }

		/// <summary>
		/// Pool for the Horde agent to assign, calculated based on platform and overrides
		/// </summary>
		public string? HordePool
		{
			get
			{
				string? Pool = DefaultHordePool;
				if (OverrideHordePool != null)
				{
					Pool = OverrideHordePool;
				}
				else if (OperatingSystem.IsWindows() && WindowsHordePool != null)
				{
					Pool = WindowsHordePool;
				}
				else if (OperatingSystem.IsMacOS() && MacHordePool != null)
				{
					Pool = MacHordePool;
				}
				else if (OperatingSystem.IsLinux() && LinuxHordePool != null)
				{
					Pool = LinuxHordePool;
				}

				//Console.WriteLine("CHOSEN UBA POOL: {0}", Pool);
				return Pool;
			}
		}

		/// <summary>
		/// Pool for the Horde agent to assign if no override current platform doesn't have it set
		/// </summary>
		[ConfigFile(ConfigHierarchyType.Engine, "Horde", "UbaPool")]
		[XmlConfigFile(Category = "Horde", Name = "Pool")]
		public string? DefaultHordePool { get; set; }

		/// <summary>
		/// Pool for the Horde agent to assign, only used for commandline override
		/// </summary>
		[CommandLine("-UBAHordePool=")]
		public string? OverrideHordePool { get; set; }

		/// <summary>
		/// Pool for the Horde agent to assign when on Linux
		/// </summary>
		[ConfigFile(ConfigHierarchyType.Engine, "Horde", "UbaLinuxPool")]
		[XmlConfigFile(Category = "Horde", Name = "LinuxPool")]
		public string? LinuxHordePool { get; set; }

		/// <summary>
		/// Pool for the Horde agent to assign when on Mac
		/// </summary>
		[ConfigFile(ConfigHierarchyType.Engine, "Horde", "UbaMacPool")]
		[XmlConfigFile(Category = "Horde", Name = "MacPool")]
		public string? MacHordePool { get; set; }

		/// <summary>
		/// Pool for the Horde agent to assign when on Windows
		/// </summary>
		[ConfigFile(ConfigHierarchyType.Engine, "Horde", "UbaWindowsPool")]
		[XmlConfigFile(Category = "Horde", Name = "WindowsPool")]
		public string? WindowsHordePool { get; set; }

		/// <summary>
		/// Requirements for the Horde agent to assign
		/// </summary>
		[ConfigFile(ConfigHierarchyType.Engine, "Horde", "UbaRequirements")]
		[XmlConfigFile(Category = "Horde", Name = "Requirements")]
		[CommandLine("-UBAHordeRequirements=")]
		public string? HordeCondition { get; set; }
		
		/// <summary>
		/// Default compute cluster ID if nothing is set
		/// </summary>
		public const string ClusterDefault = "default";
		
		/// <summary>
		/// Compute cluster ID for automatically resolving the most suitable cluster
		/// </summary>
		public const string ClusterAuto = "_auto";
		
		/// <summary>
		/// Compute cluster ID to use in Horde. Set to "_auto" to let Horde server resolve a suitable cluster.
		/// In multi-region setups this is can simplify configuration of UBT/UBA a lot.
		/// </summary>
		[ConfigFile(ConfigHierarchyType.Engine, "Horde", "UbaCluster")]
		[XmlConfigFile(Category = "Horde", Name = "Cluster")]
		[CommandLine("-UBAHordeCluster=")]
		public string? HordeCluster { get; set; }

		/// <summary>
		/// Which ip UBA server should give to agents. This will invert so host listens and agents connect
		/// </summary>
		[ConfigFile(ConfigHierarchyType.Engine, "Horde", "UbaLocalHost")]
		[XmlConfigFile(Category = "Horde", Name = "LocalHost")]
		[CommandLine("-UBAHordeHost")]
		public string HordeHost { get; set; } = String.Empty;

		/// <summary>
		/// Max cores allowed to be used by build session
		/// </summary>
		[ConfigFile(ConfigHierarchyType.Engine, "Horde", "MaxCores")]
		[XmlConfigFile(Category = "Horde", Name = "MaxCores")]
		[CommandLine("-UBAHordeMaxCores")]
		public int HordeMaxCores { get; set; } = 576;

		/// <summary>
		/// How long UBT should wait to ask for help. Useful in build configs where machine can delay remote work and still get same wall time results (pch dependencies etc)
		/// </summary>
		[ConfigFile(ConfigHierarchyType.Engine, "Horde", "UbaDelay")]
		[XmlConfigFile(Category = "Horde", Name = "StartupDelay")]
		[CommandLine("-UBAHordeDelay")]
		public int HordeDelay { get; set; } = 0;

		/// <summary>
		/// Allow use of Wine. Only applicable to Horde agents running Linux. Can still be ignored if Wine executable is not set on agent.
		/// </summary>
		[ConfigFile(ConfigHierarchyType.Engine, "Horde", "UbaAllowWine")]
		[XmlConfigFile(Category = "Horde", Name = "AllowWine")]
		[CommandLine("-UBAHordeAllowWine", Value = "true")]
		public bool bHordeAllowWine { get; set; } = true;

		/// <summary>
		/// Connection mode for agent/compute communication
		/// <see cref="ConnectionMode" /> for valid modes.
		/// </summary>
		[ConfigFile(ConfigHierarchyType.Engine, "Horde", "UbaConnectionMode")]
		[XmlConfigFile(Category = "Horde", Name = "ConnectionMode")]
		[CommandLine("-UBAHordeConnectionMode=")]
		public string? HordeConnectionMode { get; set; }

		/// <summary>
		/// Encryption to use for agent/compute communication. Note that UBA agent uses its own encryption.
		/// <see cref="Encryption" /> for valid modes.
		/// </summary>
		[ConfigFile(ConfigHierarchyType.Engine, "Horde", "UbaEncryption")]
		[XmlConfigFile(Category = "Horde", Name = "Encryption")]
		[CommandLine("-UBAHordeEncryption=")]
		public string? HordeEncryption { get; set; }

		/// <summary>
		/// Sentry URL to send box data to. Optional.
		/// </summary>
		[ConfigFile(ConfigHierarchyType.Engine, "Horde", "UbaSentryUrl")]
		[XmlConfigFile(Category = "Horde")]
		[CommandLine("-UBASentryUrl=")]
		public string? UBASentryUrl { get; set; }

		/// <summary>
		/// Disable horde all together
		/// </summary>
		[XmlConfigFile(Category = "UnrealBuildAccelerator")]
		[CommandLine("-UBADisableHorde")]
		public bool bDisableHorde { get; set; } = false;

		/// <summary>
		/// Enabled parameter for use by INI config, expects one of [True, False, BuildMachineOnly]
		/// </summary>
		[ConfigFile(ConfigHierarchyType.Engine, "Horde", "UbaEnabled")]
		public string? HordeEnabled { get; set; } = null;

	}
}