// Copyright Epic Games, Inc. All Rights Reserved.

namespace HordeServer.Configuration
{
	/// <summary>
	/// Interface for the config service
	/// </summary>
	public interface IConfigService
	{
		/// <summary>
		/// Event for notifications that the config has been updated
		/// </summary>
		event Action<ConfigUpdateInfo>? OnConfigUpdate;

		/// <summary>
		/// Validate a new set of config files. Parses and runs PostLoad methods on them.
		/// </summary>
		Task<string?> ValidateAsync(Dictionary<Uri, byte[]> files, CancellationToken cancellationToken);
	}
}
