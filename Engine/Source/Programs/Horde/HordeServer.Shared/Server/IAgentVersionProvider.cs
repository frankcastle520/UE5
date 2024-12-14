// Copyright Epic Games, Inc. All Rights Reserved.

namespace HordeServer.Server
{
	/// <summary>
	/// Allows subsystems to advertise the version of bundled tools via the /server/info endpoint
	/// </summary>
	public interface IAgentVersionProvider
	{
		/// <summary>
		/// Returns the version number of the Horde agent
		/// </summary>
		Task<string?> GetAsync(CancellationToken cancellationToken = default);
	}
}

