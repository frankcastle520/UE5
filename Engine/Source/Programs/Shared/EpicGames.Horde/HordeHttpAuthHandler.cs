// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.Linq;
using System.Net;
using System.Net.Http;
using System.Net.Http.Headers;
using System.Net.Http.Json;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;
using EpicGames.Horde.Server;
using EpicGames.OIDC;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;

namespace EpicGames.Horde
{
	/// <summary>
	/// HTTP message handler which automatically refreshes access tokens as required
	/// </summary>
	public class HordeHttpAuthHandler : DelegatingHandler
	{
		readonly HordeHttpAuthHandlerState _authState;
		readonly IOptions<HordeOptions> _options;

		/// <summary>
		/// Option for HTTP requests that can override the default behavior for whether to enable interactive auth prompts
		/// </summary>
		public static HttpRequestOptionsKey<bool> AllowInteractiveLogin { get; } = new HttpRequestOptionsKey<bool>("Horde-AllowInteractiveLogin");

		/// <summary>
		/// Constructor
		/// </summary>
		public HordeHttpAuthHandler(HttpMessageHandler innerHandler, HordeHttpAuthHandlerState authState, IOptions<HordeOptions> options)
			: base(innerHandler)
		{
			_authState = authState;
			_options = options;
		}

		/// <inheritdoc/>
		protected override async Task<HttpResponseMessage> SendAsync(HttpRequestMessage request, CancellationToken cancellationToken)
		{
			// If the request already has a custom auth header, send the request as it is
			if (request.Headers.Authorization != null)
			{
				return await base.SendAsync(request, cancellationToken);
			}

			// Check whether the request specifically allows interactive auth, otherwise fall back to the default
			bool allowInteractiveLogin;
			if (!request.Options.TryGetValue(AllowInteractiveLogin, out allowInteractiveLogin))
			{
				allowInteractiveLogin = _options.Value.AllowAuthPrompt;
			}

			// Get the current access token and send the request with that
			string? accessToken = await _authState.GetAccessTokenAsync(allowInteractiveLogin, cancellationToken);
			for (int attempt = 0; ; attempt++)
			{
				// Attempt to perform the request with this access token
				request.Headers.Authorization = (accessToken == null) ? null : new AuthenticationHeaderValue("Bearer", accessToken);
				HttpResponseMessage response = await base.SendAsync(request, cancellationToken);

				const int MaxAttempts = 3;
				if (response.StatusCode != HttpStatusCode.Unauthorized || attempt >= MaxAttempts)
				{
					return response;
				}

				// Mark this access token as invalid
				if (accessToken != null)
				{
					_authState.Invalidate(accessToken);
				}

				// Get the next token, and quit out if it's the same
				string? nextAccessToken = await _authState.GetAccessTokenAsync(allowInteractiveLogin, cancellationToken);
				if (String.Equals(accessToken, nextAccessToken, StringComparison.Ordinal))
				{
					return response;
				}

				// Otherwise update the token and try again
				accessToken = nextAccessToken;
			}
		}
	}

	/// <summary>
	/// Shared object used to track the latest access obtained token
	/// </summary>
	public sealed class HordeHttpAuthHandlerState : IAsyncDisposable
	{
		record AuthState(IClock Clock, AuthMethod Method, OidcTokenInfo? TokenInfo, bool Interactive)
		{
			public bool IsAuthorized()
			{
				bool isAnonymousAuth = Method == AuthMethod.Anonymous;
				bool isTokenValid = TokenInfo != null && TokenInfo.IsValid(Clock.UtcNow);
				return isAnonymousAuth || isTokenValid;
			}
		}

		readonly object _lockObject = new object();
		readonly HttpMessageHandler _httpMessageHandler;
		readonly Uri _serverUrl;
		readonly IOptions<HordeOptions> _options;
		readonly ILogger _logger;

		// Background auth process
		int _currentAuthTaskId;
		bool _currentAuthInteractive;
		BackgroundTask? _currentAuthWorker;
		TaskCompletionSource<AuthState>? _currentAuthResult;

		// Allow these to be overridden in tests
		readonly ITokenStore? _tokenStore;
		readonly IOidcTokenManager? _oidcTokenManager;
		readonly IClock _clock;

		/// <summary>
		/// Event handler for the auth state changing
		/// </summary>
		public event Action? OnStateChanged;

		/// <summary>
		/// Constructor
		/// </summary>
		public HordeHttpAuthHandlerState(
			HttpMessageHandler httpMessageHandler,
			Uri serverUrl,
			IOptions<HordeOptions> options,
			ILogger<HordeHttpAuthHandlerState> logger,
			ITokenStore? tokenStore = null,
			IOidcTokenManager? oidcTokenManager = null,
			IClock? clock = null)
		{
			_httpMessageHandler = httpMessageHandler;
			_serverUrl = serverUrl;
			_options = options;
			_logger = logger;
			_tokenStore = tokenStore;
			_oidcTokenManager = oidcTokenManager;
			_clock = clock ?? new DefaultClock();
		}

		/// <inheritdoc/>
		public async ValueTask DisposeAsync()
		{
			if (_currentAuthWorker != null)
			{
				await _currentAuthWorker.DisposeAsync();
				_currentAuthWorker = null;
			}
		}

		/// <summary>
		/// Checks if we have a valid auth header at the moment
		/// </summary>
		public bool IsLoggedIn()
		{
			if (GetAccessTokenFromConfig() != null)
			{
				return true;
			}
			if (TryGetCurrentAuthState(out AuthState? authState) && authState.IsAuthorized())
			{
				return true;
			}
			return false;
		}

		/// <summary>
		/// Gets the current access token
		/// </summary>
		public string? GetCurrentAccessToken()
		{
			string? accessToken = GetAccessTokenFromConfig();
			if (accessToken != null)
			{
				return accessToken;
			}
			if (TryGetCurrentAuthState(out AuthState? authState) && authState.IsAuthorized())
			{
				return authState.TokenInfo?.AccessToken;
			}
			return null;
		}

		/// <summary>
		/// Gets the current auth state instance. Fails if the current auth task has not finished.
		/// </summary>
		bool TryGetCurrentAuthState([NotNullWhen(true)] out AuthState? authState)
		{
			AuthState? state;
			if (_currentAuthResult != null && _currentAuthResult.Task.IsCompletedSuccessfully && _currentAuthResult.Task.TryGetResult(out state))
			{
				authState = state;
				return true;
			}
			else
			{
				authState = null;
				return false;
			}
		}

		/// <summary>
		/// Resets the current auth state
		/// </summary>
		public void Reset()
		{
			if (GetAccessTokenFromConfig() != null)
			{
				return;
			}

			lock (_lockObject)
			{
				_currentAuthResult = null;
			}
		}

		/// <summary>
		/// Marks the given access token as invalid, having attempted to use it and got an unauthorized response
		/// </summary>
		/// <param name="accessToken">The access  header to invalidate</param>
		public void Invalidate(string? accessToken)
		{
			if (GetAccessTokenFromConfig() != null)
			{
				return;
			}

			lock (_lockObject)
			{
				if (TryGetCurrentAuthState(out AuthState? authState) && Object.Equals(authState.TokenInfo?.AccessToken, accessToken))
				{
					_currentAuthResult = null;
				}
			}
		}

		/// <summary>
		/// Try to get a configured auth header
		/// </summary>
		public string? GetAccessTokenFromConfig()
		{
			// If an explicit access token is specified, just use that
			if (_options.Value.AccessToken != null)
			{
				return _options.Value.AccessToken;
			}

			// Check environment variables for an access token matching the current server
			string? hordeUrlEnvVar = Environment.GetEnvironmentVariable(HordeHttpClient.HordeUrlEnvVarName);
			if (!String.IsNullOrEmpty(hordeUrlEnvVar))
			{
				Uri hordeUrl = new Uri(hordeUrlEnvVar);
				if (_options.Value.ServerUrl == null || String.Equals(_options.Value.ServerUrl.Host, hordeUrl.Host, StringComparison.OrdinalIgnoreCase))
				{
					string? hordeToken = Environment.GetEnvironmentVariable(HordeHttpClient.HordeTokenEnvVarName);
					if (!String.IsNullOrEmpty(hordeToken))
					{
						return hordeToken;
					}
				}
			}

			// Otherwise we need to find the access token dynamically
			return null;
		}

		/// <summary>
		/// Gets the current access token
		/// </summary>
		public async Task<string?> GetAccessTokenAsync(bool interactive, CancellationToken cancellationToken)
		{
			// If there's a static access token configured, use that
			string? accessToken = GetAccessTokenFromConfig();
			if (accessToken != null)
			{
				return accessToken;
			}

			// Otherwise check if we need to start a background task to figure it out
			Task<AuthState> authStateTask;
			lock (_lockObject)
			{
				if (_currentAuthResult == null || (interactive && !_currentAuthInteractive))
				{
					int authTaskId = ++_currentAuthTaskId;

					if (_currentAuthResult == null || _currentAuthResult.Task.IsCompleted)
					{
						_currentAuthResult = new TaskCompletionSource<AuthState>(TaskCreationOptions.RunContinuationsAsynchronously);
					}

					BackgroundTask? prevAuthWorker = _currentAuthWorker;
					_currentAuthWorker = BackgroundTask.StartNew(ctx => GetAuthStateHandlerAsync(authTaskId, interactive, prevAuthWorker, ctx));

					_currentAuthInteractive = interactive;
				}
				authStateTask = _currentAuthResult.Task;
			}

			// Wait for the task to complete
			AuthState authState = await authStateTask.WaitAsync(cancellationToken);
			return authState?.TokenInfo?.AccessToken;
		}

		async Task GetAuthStateHandlerAsync(int authTaskId, bool interactive, BackgroundTask? prevAuthTask, CancellationToken cancellationToken)
		{
			Task? disposeTask = null;
			try
			{
				// Start disposing of the previous auth task asynchronously
				if (prevAuthTask != null)
				{
					disposeTask = Task.Run(() => prevAuthTask.DisposeAsync().AsTask(), CancellationToken.None);
				}

				// Get the new auth state
				bool stateHasChanged = false;
				try
				{
					AuthState authState = await GetAuthStateInternalAsync(interactive, cancellationToken);
					lock (_lockObject)
					{
						if (_currentAuthTaskId == authTaskId)
						{
							_logger.LogDebug("Auth task complete (interactive: {Interactive}, authorized: {Authorized})", authState.Interactive, authState.IsAuthorized());
							stateHasChanged = _currentAuthResult?.TrySetResult(authState) ?? false;
						}
					}
				}
				catch (Exception ex)
				{
					lock (_lockObject)
					{
						if (_currentAuthTaskId == authTaskId)
						{
							_logger.LogDebug(ex, "Exception while attempting auth: {Message}", ex.Message);
							stateHasChanged = _currentAuthResult?.TrySetException(ex) ?? false;
						}
					}
				}

				// Send notifications about the updated state
				if (stateHasChanged)
				{
					try
					{
						OnStateChanged?.Invoke();
					}
					catch (Exception ex)
					{
						_logger.LogError(ex, "Exception while sending state change notifications: {Message}", ex.Message);
					}
				}
			}
			finally
			{
				// Wait for the child task to finish being disposed
				if (disposeTask != null)
				{
					await disposeTask;
				}
			}
		}

		async Task<AuthState> GetAuthStateInternalAsync(bool interactive, CancellationToken cancellationToken)
		{
			GetAuthConfigResponse? authConfig;
			using (HttpClient httpClient = new HttpClient(_httpMessageHandler, false))
			{
				httpClient.BaseAddress = _serverUrl;
				_logger.LogDebug("Retrieving auth configuration for {Server}", _serverUrl);

				JsonSerializerOptions jsonOptions = new JsonSerializerOptions();
				HordeHttpClient.ConfigureJsonSerializer(jsonOptions);

				authConfig = await httpClient.GetFromJsonAsync<GetAuthConfigResponse>("api/v1/server/auth", jsonOptions, cancellationToken);
				if (authConfig == null)
				{
					throw new Exception($"Invalid response from server");
				}
			}

			if (authConfig.Method == AuthMethod.Anonymous)
			{
				return new AuthState(_clock, authConfig.Method, null, true);
			}

			string? localRedirectUrl = authConfig.LocalRedirectUrls?.FirstOrDefault();
			if (String.IsNullOrEmpty(authConfig.ServerUrl) || String.IsNullOrEmpty(localRedirectUrl))
			{
				throw new Exception("No auth server configuration found");
			}

			string oidcProvider = authConfig.ProfileName ?? "Horde";

			using ITokenStore tokenStore = CreateTokenStore();
			IOidcTokenManager oidcTokenManager = CreateOidcTokenManager(tokenStore, authConfig, localRedirectUrl);

			OidcTokenInfo? result = null;
			if (oidcTokenManager.GetStatusForProvider(oidcProvider) != OidcStatus.NotLoggedIn)
			{
				try
				{
					result = await oidcTokenManager.TryGetAccessToken(oidcProvider, cancellationToken);
				}
				catch (Exception ex)
				{
					_logger.LogTrace(ex, "Unable to get access token; attempting login: {Message}", ex.Message);
				}
			}
			if (result == null && interactive)
			{
				_logger.LogInformation("Logging in to {Server}...", _serverUrl);
				result = await oidcTokenManager.LoginAsync(oidcProvider, cancellationToken);
			}

			return new AuthState(_clock, authConfig.Method, result, interactive);
		}

		private ITokenStore CreateTokenStore()
		{
			return _tokenStore ?? TokenStoreFactory.CreateTokenStore();
		}

		private IOidcTokenManager CreateOidcTokenManager(ITokenStore tokenStore, GetAuthConfigResponse authConfig, string localRedirectUrl)
		{
			if (_oidcTokenManager != null)
			{
				return _oidcTokenManager;
			}

			string oidcProvider = authConfig.ProfileName ?? "Horde";

			Dictionary<string, string?> values = new();
			values[$"Providers:{oidcProvider}:DisplayName"] = oidcProvider;
			values[$"Providers:{oidcProvider}:ServerUri"] = authConfig.ServerUrl;
			values[$"Providers:{oidcProvider}:ClientId"] = authConfig.ClientId;
			values[$"Providers:{oidcProvider}:RedirectUri"] = localRedirectUrl;

			ConfigurationBuilder builder = new();
			builder.AddInMemoryCollection(values);
			IConfiguration configuration = builder.Build();

			return OidcTokenManager.CreateTokenManager(configuration, tokenStore, new List<string>() { oidcProvider });
		}
	}
}
