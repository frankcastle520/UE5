// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.OIDC;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.Configuration.Memory;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Net.Http;
using System.Net.Http.Json;
using System.Text.Json;
using System.Threading.Tasks;
using Serilog;
using Serilog.Events;
using System.Runtime.InteropServices;

namespace OidcToken
{
	class Program
	{
		static async Task<int> Main(string[] args)
		{
			if (args.Any(s => s.Equals("--help") || s.Equals("-help")) || args.Length == 0)
			{
				// print help
				Console.WriteLine("Usage: OidcToken [options]");
				Console.WriteLine();
				Console.WriteLine("Options: ");
				Console.WriteLine(" --Service <serviceName> - Indicate which OIDC service you intend to connect to. The connection details of the service is configured in appsettings.json/oidc-configuration.json.");
				Console.WriteLine(" --HordeUrl <url> - Specifies the URL of a Horde server to read configuration from.");
				Console.WriteLine(" --Mode [Query/GetToken] - Switch mode to allow you to preview operation without triggering user interaction (result can be used to determine if user interaction is required)");
				Console.WriteLine(" --OutFile <path> - Path to create json file of result");
				Console.WriteLine(" --ResultToConsole [true/false] - If true the resulting json file is output to stdout (and logs are not created)");
				Console.WriteLine(" --Unattended [true/false] - If true we assume no user is present and thus can not rely on their input");
				Console.WriteLine(" --Zen [true/false] - If true the resulting refresh token is posted to Zens token endpoints");
				Console.WriteLine(" --Project <path> - Project can be used to tell oidc token which game its working in to allow us to read game specific settings");

				return 0;
			}
			
			// disable reloadConfigOnChange in this process, as this can cause issues under wsl and we disable this for all configuration we actually load anyway
			Environment.SetEnvironmentVariable("DOTNET_hostBuilder:reloadConfigOnChange", "false");

			ConfigurationBuilder configBuilder = new();
			configBuilder.SetBasePath(AppContext.BaseDirectory)
				.AddJsonFile("appsettings.json", false, false)
				.AddCommandLine(args);

			IConfiguration config = configBuilder.Build();

			TokenServiceOptions options = new();
			config.Bind(options);

			GetHordeAuthConfigResponse? hordeAuthConfig = null;
			if (options.HordeUrl != null)
			{
				hordeAuthConfig = ReadHordeConfigurationAsync(options.HordeUrl).Result;
				if (hordeAuthConfig.IsAnonymous())
				{
					// Indicate to the caller that auth is disabled.
					return 11;
				}
			}

			await Host.CreateDefaultBuilder(args)
				.UseSerilog((context, configuration) =>
				{
					configuration.ReadFrom.Configuration(context.Configuration);
					if (!options.ResultToConsole)
					{
						configuration.WriteTo.Console(restrictedToMinimumLevel: LogEventLevel.Information);
					}

					// configure logging output directory match expectation per platform
					if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
					{
						configuration.WriteTo.File(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "UnrealEngine\\Common\\OidcToken\\Logs\\oidc-token.log"), rollingInterval:RollingInterval.Day, restrictedToMinimumLevel: LogEventLevel.Debug, retainedFileCountLimit: 7);
					}
					else if (RuntimeInformation.IsOSPlatform(OSPlatform.OSX))
					{
						configuration.WriteTo.File(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "Epic/UnrealEngine/Common/OidcToken/Logs/oidc-token.log"), rollingInterval: RollingInterval.Day, restrictedToMinimumLevel: LogEventLevel.Debug, retainedFileCountLimit: 7);
					}
					else if (RuntimeInformation.IsOSPlatform(OSPlatform.Linux))
					{
						configuration.WriteTo.File(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "Epic/UnrealEngine/Common/OidcToken/Logs/oidc-token.log"), rollingInterval: RollingInterval.Day, restrictedToMinimumLevel: LogEventLevel.Debug, retainedFileCountLimit: 7);
					}
				})
				.ConfigureAppConfiguration(builder =>
				{
					builder.AddConfiguration(config);

					if (hordeAuthConfig != null && !String.IsNullOrEmpty(hordeAuthConfig.ProfileName))
					{
						Dictionary<string, string?> values = new Dictionary<string, string?>();
						values[nameof(TokenServiceOptions.Service)] = hordeAuthConfig.ProfileName;
						builder.AddInMemoryCollection(values);
					}
				})
				.ConfigureServices(
				(content, services) =>
				{
					IConfiguration configuration = content.Configuration;
					services.AddOptions<TokenServiceOptions>().Bind(configuration).ValidateDataAnnotations();

					IConfiguration serviceConfig;
					if (hordeAuthConfig != null)
					{
						Dictionary<string, string?> values = new Dictionary<string, string?>();
						values[$"{nameof(OidcTokenOptions.Providers)}:{hordeAuthConfig.ProfileName}:{nameof(ProviderInfo.DisplayName)}"] = hordeAuthConfig.ProfileName;
						values[$"{nameof(OidcTokenOptions.Providers)}:{hordeAuthConfig.ProfileName}:{nameof(ProviderInfo.ServerUri)}"] = hordeAuthConfig.ServerUrl;
						values[$"{nameof(OidcTokenOptions.Providers)}:{hordeAuthConfig.ProfileName}:{nameof(ProviderInfo.ClientId)}"] = hordeAuthConfig.ClientId;
						values[$"{nameof(OidcTokenOptions.Providers)}:{hordeAuthConfig.ProfileName}:{nameof(ProviderInfo.RedirectUri)}"] = hordeAuthConfig.LocalRedirectUrls![0];
						serviceConfig = new ConfigurationBuilder().AddInMemoryCollection(values).Build();
					}
					else
					{
						// guess where the engine directory is based on the assumption that we are running out of Engine\Binaries\DotNET\OidcToken\<platform>
						DirectoryInfo engineDir = new DirectoryInfo(Path.Combine(AppContext.BaseDirectory, "../../../../../Engine"));
						if (!engineDir.Exists)
						{
							// try to see if engine dir can be found from the current code path Engine\Source\Programs\OidcToken\bin\<Configuration>\<.net-version>
							engineDir = new DirectoryInfo(Path.Combine(AppContext.BaseDirectory, "../../../../../../../Engine"));

							if (!engineDir.Exists)
							{
								throw new Exception($"Unable to guess engine directory so unable to continue running. Starting directory was: {AppContext.BaseDirectory}");
							}
						}

						serviceConfig = ProviderConfigurationFactory.ReadConfiguration(engineDir, !string.IsNullOrEmpty(options.Project) ? new DirectoryInfo(options.Project) : null);
					}
					services.AddOptions<OidcTokenOptions>().Bind(serviceConfig).ValidateDataAnnotations();

					services.AddSingleton<OidcTokenManager>();
					services.AddSingleton<ITokenStore>(TokenStoreFactory.CreateTokenStore);

					services.AddHostedService<TokenService>();
				})
				.RunConsoleAsync();

			return Environment.ExitCode;
		}

		class GetHordeAuthConfigResponse
		{
			public string Method { get; set; } = String.Empty;
			public string ProfileName { get; set; } = null!;
			public string ServerUrl { get; set; } = null!;
			public string ClientId { get; set; } = null!;
			public List<string> LocalRedirectUrls { get; set; } = new List<string>();

			public bool IsAnonymous() => Method.Equals("Anonymous", StringComparison.OrdinalIgnoreCase);
		}

		static async Task<GetHordeAuthConfigResponse> ReadHordeConfigurationAsync(Uri hordeUrl)
		{
			// Read the configuration settings from the Horde server
			GetHordeAuthConfigResponse? authConfig;
			using (HttpClient httpClient = new HttpClient())
			{
				using HttpRequestMessage request = new HttpRequestMessage(HttpMethod.Get, new Uri(hordeUrl, "api/v1/server/auth"));
				using HttpResponseMessage response = await httpClient.SendAsync(request);
				response.EnsureSuccessStatusCode();

				JsonSerializerOptions jsonOptions = new JsonSerializerOptions { PropertyNameCaseInsensitive = true };
				authConfig = await response.Content.ReadFromJsonAsync<GetHordeAuthConfigResponse>(jsonOptions);

				if (authConfig == null)
				{
					throw new InvalidDataException("Server returned an empty auth config object");
				}
			}

			if (!authConfig.IsAnonymous())
			{
				string? localRedirectUrl = authConfig.LocalRedirectUrls.FirstOrDefault();
				if (String.IsNullOrEmpty(authConfig.ServerUrl) || String.IsNullOrEmpty(authConfig.ClientId) || String.IsNullOrEmpty(localRedirectUrl))
				{
					throw new Exception("No auth server configuration found");
				}

				if (String.IsNullOrEmpty(authConfig.ProfileName))
				{
					authConfig.ProfileName = hordeUrl.Host.ToString();
				}
			}

			return authConfig;
		}
	}
}