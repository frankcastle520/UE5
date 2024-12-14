// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Horde.Secrets;
using HordeServer.Plugins;
using HordeServer.Secrets;
using HordeServer.Secrets.Providers;
using Microsoft.AspNetCore.Builder;
using Microsoft.Extensions.DependencyInjection;

namespace HordeServer
{
	/// <summary>
	/// Entry point for the storage plugin
	/// </summary>
	[Plugin("Secrets", GlobalConfigType = typeof(SecretsConfig))]
	public class SecretsPlugin : IPluginStartup
	{
		/// <inheritdoc/>
		public void Configure(IApplicationBuilder app)
		{ }

		/// <inheritdoc/>
		public void ConfigureServices(IServiceCollection services)
		{
			services.AddSingleton<SecretCollectionInternal>();
			services.AddScoped<ISecretCollection, SecretCollection>();
			services.AddSingleton<ISecretProvider, AwsParameterStoreSecretProvider>();
		}
	}

	/// <summary>
	/// Helper methods for secrets config
	/// </summary>
	public static class SecretsPluginExtensions
	{
		/// <summary>
		/// Configures the secrets plugin
		/// </summary>
		public static void AddSecretsConfig(this IDictionary<PluginName, IPluginConfig> dictionary, SecretsConfig secretsConfig)
			=> dictionary[new PluginName("Secrets")] = secretsConfig;
	}
}
