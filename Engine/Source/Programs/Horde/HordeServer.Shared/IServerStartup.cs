// Copyright Epic Games, Inc. All Rights Reserved.

using Microsoft.Extensions.DependencyInjection;

namespace HordeServer
{
	/// <summary>
	/// Interface for the main server startup class. Can be used by commands that want to instantiate server services.
	/// </summary>
	public interface IServerStartup
	{
		/// <summary>
		/// Configure services for the server
		/// </summary>
		void ConfigureServices(IServiceCollection services);
	}
}
