// Copyright Epic Games, Inc. All Rights Reserved.

namespace HordeServer.Secrets
{
	/// <summary>
	/// Interface for a service that can provide secret data
	/// </summary>
	public interface ISecretProvider
	{
		/// <summary>
		/// Name of this provider
		/// </summary>
		string Name { get; }

		/// <summary>
		/// Gets values for a particular secret
		/// </summary>
		/// <param name="path">Path to the secret to fetch</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		Task<string> GetSecretAsync(string path, CancellationToken cancellationToken);
	}
}
