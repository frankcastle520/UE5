// Copyright Epic Games, Inc. All Rights Reserved.

using System.ComponentModel;
using EpicGames.Core;
using EpicGames.Horde;
using EpicGames.Horde.Server;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;

namespace Horde.Commands
{
	[Command("login", "Logs in to a Horde server")]
	class LoginCommand : Command
	{
		class GetAuthConfigResponse
		{
			public string? Method { get; set; }
			public string? ServerUrl { get; set; }
			public string? ClientId { get; set; }
			public string[]? RedirectUrls { get; set; }
		}

		[CommandLine("-Token")]
		[Description("Echo the bearer token acquired from the server to stdout")]
		public bool Token { get; set; }

		readonly IServiceProvider _serviceProvider;
		readonly IHordeClient _hordeClient;

		public LoginCommand(IServiceProvider serviceProvider, IHordeClient hordeClient)
		{
			_serviceProvider = serviceProvider;
			_hordeClient = hordeClient;
		}

		/// <inheritdoc/>
		public override async Task<int> ExecuteAsync(ILogger logger)
		{
			using HordeHttpClient httpClient = _hordeClient.CreateHttpClient();

			GetServerInfoResponse serverInfo = await httpClient.GetServerInfoAsync();
			logger.LogInformation("Connected to server version: {Version}", serverInfo.ServerVersion);

			if (Token)
			{
				HordeHttpAuthHandlerState state = _serviceProvider.GetRequiredService<HordeHttpAuthHandlerState>();

				string? accessToken = await state.GetAccessTokenAsync(true, CancellationToken.None);
				if (accessToken != null)
				{
					Console.WriteLine($"Bearer {accessToken}");
				}
			}

			return 0;
		}
	}
}
