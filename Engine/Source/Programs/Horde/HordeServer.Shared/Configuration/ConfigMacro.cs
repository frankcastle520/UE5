// Copyright Epic Games, Inc. All Rights Reserved.

namespace HordeServer.Configuration
{
	/// <summary>
	/// Declares a config macro
	/// </summary>
	public class ConfigMacro
	{
		/// <summary>
		/// Name of the macro property
		/// </summary>
		public string Name { get; set; } = String.Empty;

		/// <summary>
		/// Value for the macro property
		/// </summary>
		public string Value { get; set; } = String.Empty;
	}
}
