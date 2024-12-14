// Copyright Epic Games, Inc. All Rights Reserved.

using System.Collections.Generic;

namespace HordeServer.Plugins
{
	/// <summary>
	/// Interface for querying the state of plugins on the server
	/// </summary>
	public interface IPluginCollection
	{
		/// <summary>
		/// List of available plugins
		/// </summary>
		IReadOnlyList<IPlugin> Plugins { get; }

		/// <summary>
		/// List of the enabled plugins
		/// </summary>
		IReadOnlyList<ILoadedPlugin> LoadedPlugins { get; }
	}
}
