// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Plugins;

namespace HordeServer.Tests
{
	static class PluginConfigExtensions
	{
		public static void AddAnalytics(this PluginConfigCollection configCollection, AnalyticsConfig config)
			=> configCollection[new PluginName("Analytics")] = config;
	}
}
