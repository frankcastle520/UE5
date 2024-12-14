// Copyright Epic Games, Inc. All Rights Reserved.

using System.Diagnostics.CodeAnalysis;
using System.Security.Claims;
using EpicGames.Horde.Acls;
using EpicGames.Horde.Secrets;
using HordeServer.Plugins;
using HordeServer.Secrets;

#pragma warning disable CA2227 // Change x to be read-only by removing the property setter

namespace HordeServer
{
	/// <summary>
	/// Configuration for the secrets system
	/// </summary>
	public class SecretsConfig : IPluginConfig
	{
		/// <summary>
		/// List of secrets
		/// </summary>
		public List<SecretConfig> Secrets { get; set; } = new List<SecretConfig>();

		private readonly Dictionary<SecretId, SecretConfig> _secretLookup = new Dictionary<SecretId, SecretConfig>();

		/// <inheritdoc/>
		public void PostLoad(PluginConfigOptions configOptions)
		{
			_secretLookup.Clear();
			foreach (SecretConfig secret in Secrets)
			{
				_secretLookup.Add(secret.Id, secret);
				secret.PostLoad(configOptions.ParentAcl);
			}
		}

		/// <summary>
		/// Attempts to get a secret configuration from this object
		/// </summary>
		/// <param name="secretId">Secret id</param>
		/// <param name="config">Receives the secret configuration on success</param>
		/// <returns>True on success</returns>
		public bool TryGetSecret(SecretId secretId, [NotNullWhen(true)] out SecretConfig? config)
			=> _secretLookup.TryGetValue(secretId, out config);

		/// <summary>
		/// Authorize access to a secret
		/// </summary>
		public bool Authorize(SecretId secretId, AclAction action, ClaimsPrincipal user)
			=> TryGetSecret(secretId, out SecretConfig? secretConfig) && secretConfig.Authorize(action, user);
	}
}
