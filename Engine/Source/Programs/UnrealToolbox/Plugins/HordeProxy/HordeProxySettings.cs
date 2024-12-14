// Copyright Epic Games, Inc. All Rights Reserved.

#pragma warning disable CS1591

namespace UnrealToolbox.Plugins.HordeProxy
{
	[Serializable]
	public class HordeProxySettings
	{
		public bool Enabled { get; set; }
		public int Port { get; set; } = 13344;
	}
}
