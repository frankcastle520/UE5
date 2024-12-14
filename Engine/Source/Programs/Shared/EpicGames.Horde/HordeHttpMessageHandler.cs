// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Net;
using System.Net.Http;
using System.Threading.Tasks;
using EpicGames.Core;
using Microsoft.Extensions.Http;
using Microsoft.Extensions.Logging;
using Polly;
using Polly.Retry;
using Polly.Timeout;

namespace EpicGames.Horde
{
	/// <summary>
	/// Concrete implementation of <see cref="IHordeHttpMessageHandler"/> which manages the lifetime of the <see cref="HttpMessageHandler"/> instance.
	/// </summary>
	class HordeHttpMessageHandler : IHordeHttpMessageHandler, IDisposable
	{
		readonly HttpMessageHandler _instance;

		/// <inheritdoc/>
		public HttpMessageHandler Instance => _instance;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="logger"></param>
		public HordeHttpMessageHandler(ILogger<HordeHttpMessageHandler> logger)
		{
			_instance = new SocketsHttpHandler();
			_instance = new PolicyHttpMessageHandler(request => CreateDefaultTimeoutRetryPolicy(request, logger)) { InnerHandler = _instance };
			_instance = new PolicyHttpMessageHandler(request => CreateDefaultTransientErrorPolicy(request, logger)) { InnerHandler = _instance };
		}

		/// <inheritdoc/>
		public void Dispose()
			=> _instance.Dispose();

		/// <summary>
		/// Create a default timeout retry policy
		/// </summary>
		static IAsyncPolicy<HttpResponseMessage> CreateDefaultTimeoutRetryPolicy(HttpRequestMessage request, ILogger logger)
		{
			// Wait 30 seconds for operations to timeout
			Task OnTimeoutAsync(Context context, TimeSpan timespan, Task timeoutTask)
			{
				logger.LogWarning(KnownLogEvents.Systemic_Horde_Http, "{Method} {Url} timed out after {Time}s.", request.Method, request.RequestUri, (int)timespan.TotalSeconds);
				return Task.CompletedTask;
			}

			AsyncTimeoutPolicy<HttpResponseMessage> timeoutPolicy = Policy.TimeoutAsync<HttpResponseMessage>(60, OnTimeoutAsync);

			// Retry twice after a timeout
			void OnRetry(Exception ex, TimeSpan timespan)
			{
				logger.LogWarning(KnownLogEvents.Systemic_Horde_Http, ex, "{Method} {Url} retrying after {Time}s.", request.Method, request.RequestUri, timespan.TotalSeconds);
			}

			TimeSpan[] retryTimes = new[] { TimeSpan.FromSeconds(5.0), TimeSpan.FromSeconds(10.0) };
			AsyncRetryPolicy retryPolicy = Policy.Handle<TimeoutRejectedException>().WaitAndRetryAsync(retryTimes, OnRetry);
			return retryPolicy.WrapAsync(timeoutPolicy);
		}

		/// <summary>
		/// Create a default timeout retry policy
		/// </summary>
		static IAsyncPolicy<HttpResponseMessage> CreateDefaultTransientErrorPolicy(HttpRequestMessage request, ILogger logger)
		{
			Task OnTimeoutAsync(DelegateResult<HttpResponseMessage> outcome, TimeSpan timespan, int retryAttempt, Context context)
			{
				logger.LogInformation(KnownLogEvents.Systemic_Horde_Http, "{Method} {Url} failed ({Result}). Delaying for {DelayMs}ms (attempt #{RetryNum}).", request.Method, request.RequestUri, outcome.Result?.StatusCode, timespan.TotalMilliseconds, retryAttempt);
				return Task.CompletedTask;
			}

			TimeSpan[] retryTimes = new[] { TimeSpan.FromSeconds(1.0), TimeSpan.FromSeconds(5.0), TimeSpan.FromSeconds(10.0), TimeSpan.FromSeconds(30.0), TimeSpan.FromSeconds(30.0) };

			// Policy for transient errors is the same as HttpPolicyExtensions.HandleTransientHttpError(), but excludes HttpStatusCode.ServiceUnavailable (which is used as a response
			// when allocating compute resources when none are available). This pathway is handled explicitly on the application side.
			return Policy<HttpResponseMessage>
				.Handle<HttpRequestException>()
				.OrResult(x => (x.StatusCode >= HttpStatusCode.InternalServerError && x.StatusCode != HttpStatusCode.ServiceUnavailable) || x.StatusCode == HttpStatusCode.RequestTimeout)
				.WaitAndRetryAsync(retryTimes, OnTimeoutAsync);
		}
	}
}
