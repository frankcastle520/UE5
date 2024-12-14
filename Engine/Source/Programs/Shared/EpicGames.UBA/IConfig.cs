// Copyright Epic Games, Inc. All Rights Reserved.

namespace EpicGames.UBA
{
	/// <summary>
	/// Base interface for uba config file
	/// </summary>
	public interface IConfig
	{
		/// <summary>
		/// Load a config file
		/// </summary>
		/// <param name="configFile">The name of the config file</param>
		/// <returns>The IConfig</returns>
		public static IConfig LoadConfig(string configFile)
		{
			return new ConfigImpl(configFile);
		}
	}
}