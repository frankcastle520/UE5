// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;

namespace EpicGames.OIDC;

/// <summary>
/// An in-memory token store without encryption, intended for testing
/// </summary>
public sealed class InMemoryTokenStore : ITokenStore
{
	private readonly Dictionary<string, string> _providerToRefreshToken = new ();

	/// <inheritdoc/>
	public bool TryGetRefreshToken(string oidcProvider, out string refreshToken)
	{
		if (!_providerToRefreshToken.TryGetValue(oidcProvider, out string? result))
		{
			refreshToken = "";
			return false;
		}

		refreshToken = result;
		return true;
	}

	/// <inheritdoc/>
	public void AddRefreshToken(string providerIdentifier, string refreshToken)
	{
		_providerToRefreshToken[providerIdentifier] = refreshToken;
	}

	/// <inheritdoc/>
	public void Save()
	{
		// No need - data exists only in-memory
	}

	/// <inheritdoc/>
	public void Dispose()
	{
		GC.SuppressFinalize(this);
	}
}
