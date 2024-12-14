// Copyright Epic Games, Inc. All Rights Reserved.

using Microsoft.AspNetCore.Builder;
using Microsoft.Extensions.DependencyInjection;

namespace HordeServer.Plugins
{
	/// <summary>
	/// Interface for modules that can extend the services known by Horde
	/// </summary>
	public interface IPluginStartup
	{
		/// <summary>
		/// Configure the application
		/// </summary>
		/// <param name="app">Application builder instance</param>
		void Configure(IApplicationBuilder app);

		/// <summary>
		/// Configure the services provided by the plugin
		/// </summary>
		/// <param name="serviceCollection">Collection of services to add to</param>
		void ConfigureServices(IServiceCollection serviceCollection);
	}
}
