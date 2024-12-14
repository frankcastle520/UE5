// Copyright Epic Games, Inc. All Rights Reserved.

using System.Collections.Generic;

namespace EpicGames.Horde.Secrets
{
	/// <summary>
	/// Information about a secret
	/// </summary>
	public interface ISecret
	{
		/// <summary>
		/// Identifier for the secret
		/// </summary>
		SecretId Id { get; }

		/// <summary>
		/// The secret values
		/// </summary>
		IReadOnlyDictionary<string, string> Data { get; }
	}
}
