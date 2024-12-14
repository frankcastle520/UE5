// Copyright Epic Games, Inc. All Rights Reserved.

using System.Runtime.InteropServices;

namespace EpicGames.UBA
{
	internal class ConfigImpl : IConfig
	{
		#region DllImport
		[DllImport("UbaHost", CharSet = CharSet.Auto)]
		static extern bool Config_Load(string configFile);
		#endregion

		public ConfigImpl(string configFile)
		{
			Config_Load(configFile);
		}
	}
}