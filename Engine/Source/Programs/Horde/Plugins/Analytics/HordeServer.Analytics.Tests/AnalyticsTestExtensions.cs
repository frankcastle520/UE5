// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Plugins;

namespace HordeServer.Analytics.Tests
{
	public static class AnalyticsTestExtensions
	{
		public static void AddAnalyticsTestConfig(this PluginConfigCollection configCollection, AnalyticsConfig config)
			=> configCollection[new PluginName("Analytics")] = config;
	}
}
