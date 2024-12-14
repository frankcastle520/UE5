// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Plugins;
using HordeServer.Symbols;
using Microsoft.AspNetCore.Builder;
using Microsoft.Extensions.DependencyInjection;

namespace HordeServer
{
	/// <summary>
	/// Entry point for the symbols plugin
	/// </summary>
	[Plugin("Symbols", GlobalConfigType = typeof(SymbolsConfig))]
	public class SymbolsPlugin : IPluginStartup
	{
		/// <inheritdoc/>
		public void Configure(IApplicationBuilder app)
		{ }

		/// <inheritdoc/>
		public void ConfigureServices(IServiceCollection services)
		{ }
	}
}
