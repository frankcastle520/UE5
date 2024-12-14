// Copyright Epic Games, Inc. All Rights Reserved.

using System.Threading;
using System.Threading.Tasks;

namespace EpicGames.Horde.Secrets
{
	/// <summary>
	/// Collection of secrets
	/// </summary>
	public interface ISecretCollection
	{
		/// <summary>
		/// Resolve a secret to concrete values
		/// </summary>
		/// <param name="secretId">Identifier for the secret</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		Task<ISecret?> GetAsync(SecretId secretId, CancellationToken cancellationToken = default);
	}
}
