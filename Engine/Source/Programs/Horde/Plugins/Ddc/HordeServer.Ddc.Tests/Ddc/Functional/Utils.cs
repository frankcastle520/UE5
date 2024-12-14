// Copyright Epic Games, Inc. All Rights Reserved.

using System.Text.Json;
using HordeServer.Utilities;

namespace HordeServer.Tests.Ddc.FunctionalTests
{
	public static class JsonTestUtils
	{
		public static readonly JsonSerializerOptions DefaultJsonSerializerSettings = ConfigureJsonOptions();

		private static JsonSerializerOptions ConfigureJsonOptions()
		{
			JsonSerializerOptions options = new JsonSerializerOptions();
			JsonUtils.ConfigureJsonSerializer(options);
			return options;
		}
	}
}
