// Copyright Epic Games, Inc. All Rights Reserved.

namespace HordeServer.Plugins
{
	/// <summary>
	/// Base class for plugin server config objects
	/// </summary>
	public class PluginServerConfig
	{
		/// <summary>
		/// Whether the plugin should be enabled or not
		/// </summary>
		public bool Enabled { get; set; } = true;
	}
}
