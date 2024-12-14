// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Ddc;
using HordeServer.Plugins;
using Microsoft.AspNetCore.Builder;
using Microsoft.Extensions.DependencyInjection;

namespace HordeServer
{
	/// <summary>
	/// Entry point for the ddc plugin
	/// </summary>
	[Plugin("Ddc")]
	public class DdcPlugin : IPluginStartup
	{
		/// <inheritdoc/>
		public void Configure(IApplicationBuilder app)
		{ }

		/// <inheritdoc/>
		public void ConfigureServices(IServiceCollection services)
		{
			services.AddScoped<IRequestHelper, RequestHelper>();
			services.AddScoped<IBlobService, BlobService>();
			services.AddScoped<IRefService, RefService>();
			services.AddScoped<IReferenceResolver, ReferenceResolver>();
			services.AddScoped<IContentIdStore, ContentIdStore>();
			services.AddSingleton<BufferedPayloadFactory>();
			services.AddSingleton<NginxRedirectHelper>();
			services.AddSingleton<FormatResolver>();
			services.AddSingleton<CompressedBufferUtils>();
		}
	}
}
