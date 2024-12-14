// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Diagnostics.Metrics;
using System.IO;
using System.Net;
using System.Net.Http;
using System.Runtime.CompilerServices;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;
using EpicGames.Horde.Storage;
using Jupiter.Common;
using Jupiter.Common.Implementation;
using Jupiter.Controllers;
using Microsoft.AspNetCore.Mvc;
using Microsoft.Extensions.Logging;
using OpenTelemetry.Trace;

namespace Jupiter.Implementation
{
	public class BlobsReplicator : IReplicator
	{
		private readonly string _name;
		private readonly ILogger _logger;
		private readonly ManualResetEvent _replicationFinishedEvent = new ManualResetEvent(true);
		private readonly CancellationTokenSource _replicationTokenSource = new CancellationTokenSource();
		private readonly ReplicatorSettings _replicatorSettings;
		private readonly IBlobService _blobService;
		private readonly IReplicationLog _replicationLog;
		private readonly IServiceCredentials _serviceCredentials;
		private readonly Tracer _tracer;
		private readonly BufferedPayloadFactory _bufferedPayloadFactory;
		private readonly HttpClient _httpClient;
		private readonly NamespaceId _namespace;
		private BlobsState _replicationState;
		private bool _replicationRunning;
		private bool _disposed = false;

		private static Histogram<long>? s_replicatedCounter;
		private static Histogram<long>? s_replicationBehindCounter;
		private static Counter<long>? s_replicationAttempts;

		private static readonly JsonSerializerOptions DefaultSerializerSettings = ConfigureJsonOptions();
		public BlobsReplicator(ReplicatorSettings replicatorSettings, IBlobService blobService, IHttpClientFactory httpClientFactory, IReplicationLog replicationLog, IServiceCredentials serviceCredentials, Tracer tracer, BufferedPayloadFactory bufferedPayloadFactory,ILogger<BlobsReplicator> logger, Meter meter)
		{
			_name = replicatorSettings.ReplicatorName;
			_namespace = new NamespaceId(replicatorSettings.NamespaceToReplicate);
			_replicatorSettings = replicatorSettings;
			_blobService = blobService;
			_replicationLog = replicationLog;
			_serviceCredentials = serviceCredentials;
			_tracer = tracer;
			_bufferedPayloadFactory = bufferedPayloadFactory;
			_logger = logger;

			_httpClient = httpClientFactory.CreateClient();
			_httpClient.BaseAddress = new Uri(replicatorSettings.ConnectionString);

			ReplicatorState? replicatorState = _replicationLog.GetReplicatorStateAsync(_namespace, _name).Result;
			if (replicatorState == null)
			{
				_replicationState = new BlobsState();
			}
			else
			{
				_replicationState = new BlobsState()
				{
					LastBucket = replicatorState.LastBucket,
				};
			}

			Info = new ReplicatorInfo(replicatorSettings.ReplicatorName, _namespace, _replicationState);

			if (s_replicatedCounter == null)
			{
				s_replicatedCounter = meter.CreateHistogram<long>("blob-replication.active");
			}
			if (s_replicationBehindCounter == null)
			{
				s_replicationBehindCounter = meter.CreateHistogram<long>("blob-replication.queue-length");
			}
			if (s_replicationAttempts == null)
			{
				s_replicationAttempts = meter.CreateCounter<long>("blob-replication.attempts");
			}
		}

		private static JsonSerializerOptions ConfigureJsonOptions()
		{
			JsonSerializerOptions options = new JsonSerializerOptions();
			BaseStartup.ConfigureJsonOptions(options);
			return options;
		}

		public void Dispose()
		{
			Dispose(true);
			GC.SuppressFinalize(this);
		}

		protected virtual void Dispose(bool disposing)
		{
			if (_disposed)
			{
				return;
			}

			if (disposing)
			{
				_replicationFinishedEvent.WaitOne();
				_replicationFinishedEvent.Dispose();
				_replicationTokenSource.Dispose();

				_httpClient.Dispose();
			}

			_disposed = true;
		}

		private async Task SaveStateAsync(BlobsState newState)
		{
			await _replicationLog.UpdateReplicatorStateAsync(Info.NamespaceToReplicate, _name, new ReplicatorState { LastBucket = newState.LastBucket });
		}

		public async Task<bool> TriggerNewReplicationsAsync()
		{
			if (_replicationRunning)
			{
				_logger.LogDebug("Skipping replication of replicator: {Name} as it was already running.", _name);
				return false;
			}

			// read the state again to allow it to be modified by the admin controller / other instances of jupiter connected to the same filesystem
			ReplicatorState? replicatorState = await _replicationLog.GetReplicatorStateAsync(_namespace, _name);
			if (replicatorState == null)
			{
				_replicationState = new BlobsState();
			}
			else
			{
				_replicationState = new BlobsState()
				{
					LastBucket = replicatorState.LastBucket,
				};
			}

			_logger.LogDebug("Read Replication state for replicator: {Name}. {LastBucket} {LastEvent}.", _name, _replicationState.LastBucket, _replicationState.LastEvent);

			LogReplicationHeartbeat();

			bool hasRun;
			int countOfReplicationsDone = 0;

			try
			{
				_logger.LogDebug("Replicator: {Name} is starting a run.", _name);

				_replicationTokenSource.TryReset();
				_replicationRunning = true;
				_replicationFinishedEvent.Reset();
				CancellationToken replicationToken = _replicationTokenSource.Token;

				NamespaceId ns = _namespace;

				Info.CountOfRunningReplications = 0;

				string? lastBucket = _replicationState.LastBucket;
				if (lastBucket == null)
				{
					// if we have not run start from a bucket 7 days ago and replicate from there
					lastBucket = DateTime.UtcNow.AddDays(-7).ToReplicationBucket().ToReplicationBucketIdentifier();
				}

				try
				{
					countOfReplicationsDone += await ReplicateIncrementallyAsync(ns, lastBucket, replicationToken);
				}
				catch (ToOldTimeBucketException)
				{
					_logger.LogError("Encountered old bucket in use, resetting to replicating only the last weeks data. Regions will likely be inconsistent.");
					string bucket = DateTime.UtcNow.AddDays(-7).ToReplicationBucket().ToReplicationBucketIdentifier();
					_replicationState.LastBucket = bucket;
					await SaveStateAsync(_replicationState);
				}

				hasRun = countOfReplicationsDone != 0;
			}
			finally
			{
				_replicationRunning = false;
				_replicationFinishedEvent.Set();
			}

			_logger.LogDebug("Replicator: {Name} finished its replication run. Replications completed: {ReplicationsDone} .", _name, countOfReplicationsDone);

			return hasRun;
		}

		private void LogReplicationHeartbeat()
		{
			s_replicationAttempts?.Add(1, new KeyValuePair<string, object?>("replicator", _name), new KeyValuePair<string, object?>("namespace", _namespace));
		}

		private async Task<HttpRequestMessage> BuildHttpRequestAsync(HttpMethod httpMethod, Uri uri)
		{
			string? token = await _serviceCredentials.GetTokenAsync();
			HttpRequestMessage request = new HttpRequestMessage(httpMethod, uri);
			if (!string.IsNullOrEmpty(token))
			{
				request.Headers.Add("Authorization", $"{_serviceCredentials.GetAuthenticationScheme()} {token}");
			}

			return request;
		}

		private async Task<int> ReplicateIncrementallyAsync(NamespaceId ns, string lastBucket, CancellationToken replicationToken)
		{
			int countOfReplicationsDone = 0;

			// if MaxParallelReplications is set to not limit we use the default behavior of ParallelForEachAsync which is to limit based on CPUs 
			int maxParallelism = _replicatorSettings.MaxParallelReplications != -1 ? _replicatorSettings.MaxParallelReplications : 0;

			_logger.LogInformation("{Name} Starting blob replication maxParallelism: {MaxParallelism} Last Bucket {LastBucket}", _name, maxParallelism, lastBucket);

			using CancellationTokenSource cancellationTokenSource = new CancellationTokenSource();
			using CancellationTokenSource linkedTokenSource = CancellationTokenSource.CreateLinkedTokenSource(cancellationTokenSource.Token, replicationToken);

			if (replicationToken.IsCancellationRequested)
			{
				return countOfReplicationsDone;
			}

			// log the metrics when we start / have nothing to do to make sure there is data in these most of the time
			int timeBucketsBehindStart = (int)(DateTime.UtcNow - lastBucket.FromReplicationBucketIdentifier()).TotalMinutes / 5;
			s_replicationBehindCounter?.Record(timeBucketsBehindStart, new KeyValuePair<string, object?>("replicator", _name), new KeyValuePair<string, object?>("namespace", _namespace));
			s_replicatedCounter?.Record(countOfReplicationsDone, new KeyValuePair<string, object?>("replicator", _name), new KeyValuePair<string, object?>("namespace", _namespace));

			foreach (string refBucket in GetRefBuckets(lastBucket, replicationToken))
			{
				if (replicationToken.IsCancellationRequested)
				{
					break;
				}

				LogReplicationHeartbeat();

				await Parallel.ForEachAsync(GetBlobEventsAsync(ns, refBucket, replicationToken),
					new ParallelOptions { MaxDegreeOfParallelism = maxParallelism, CancellationToken = linkedTokenSource.Token },
					async (BlobReplicationLogEvent @event, CancellationToken ctx) =>
				{
					using TelemetrySpan scope = _tracer.StartActiveSpan("replicator.replicate_blob")
						.SetAttribute("operation.name", "replicator.replicate_blob")
						.SetAttribute("resource.name", $"{ns}.{@event.Blob}")
						.SetAttribute("time-bucket", refBucket);

					_logger.LogDebug("{Name} New transaction to replicate found. Blob: {Namespace} {Blob} ({Bucket}) in {TimeBucket} ({TimeDate}).", _name, @event.Namespace, @event.Blob, @event.BucketHint.HasValue ? @event.BucketHint.Value : "Unknown-Bucket", @event.TimeBucket, @event.Timestamp);

					Interlocked.Increment(ref countOfReplicationsDone);

					try
					{
						bool blobWasReplicated = false;
						// we do not need to replicate delete events
						if (@event.Op != BlobReplicationLogEvent.OpType.Deleted)
						{
							blobWasReplicated = await ReplicateBlobAsync(@event.Namespace, @event.Blob, @event.BucketHint, linkedTokenSource.Token);
						}

						if (blobWasReplicated)
						{
							await AddToBlobReplicationLogAsync(@event.Namespace, @event.Blob, @event.BucketHint);
						}
					}
					catch (BlobNotFoundException)
					{
						_logger.LogError("{Name} Failed to replicate {@Op} in {Namespace} because blob was not present in remote store. Skipping.", _name, @event, Info.NamespaceToReplicate);
					}
				});

				// finished replicating bucket
				DateTime timestamp = refBucket.FromReplicationBucketIdentifier();

				// if the bucket was recently created we do not store that we replicate it, thus we will replicate it again to avoid risks with missing recent blobs that gets added after we queries the time bucket
				bool isRecentBucket = timestamp > DateTime.UtcNow.AddMinutes(-10);

				if (isRecentBucket)
				{
					_logger.LogInformation("Reached recent bucket {BucketName} when replicating {Name}, will run replication again to ensure consistency as this bucket may change again", refBucket, _name);
				}
				else
				{
					// we have replicated everything up to a point and can persist this in the state
					_replicationState.LastBucket = refBucket;
					await SaveStateAsync(_replicationState);
				}

				Info.LastRun = DateTime.Now;
				int timeBucketsBehind = (int)(DateTime.UtcNow - timestamp).TotalMinutes / 5;
				s_replicationBehindCounter?.Record(timeBucketsBehind, new KeyValuePair<string, object?>("replicator", _name), new KeyValuePair<string, object?>("namespace", _namespace));
				s_replicatedCounter?.Record(countOfReplicationsDone, new KeyValuePair<string, object?>("replicator", _name), new KeyValuePair<string, object?>("namespace", _namespace));

				_logger.LogInformation("{Name} replicated all events up to {Time} . Bucket: {EventBucket}", _name, timestamp, refBucket);
			}

			return countOfReplicationsDone;
		}

		private async IAsyncEnumerable<BlobReplicationLogEvent> GetBlobEventsAsync(NamespaceId ns, string refBucket, [EnumeratorCancellation] CancellationToken cancellationToken)
		{
			if (cancellationToken.IsCancellationRequested)
			{
				yield break;
			}

			HttpResponseMessage? response = null;
			Exception? lastException = null;
			const int RetryAttempts = 3;
			for (int i = 0; i < RetryAttempts; i++)
			{
				using HttpRequestMessage request = await BuildHttpRequestAsync(HttpMethod.Get, new Uri($"/api/v1/replication-log/blobs/{ns}/{refBucket}", UriKind.Relative));

				try
				{
					response = await _httpClient.SendAsync(request, cancellationToken);
					break;
				}
				catch (HttpRequestException e)
				{
					response = null;

					if (e.StatusCode == HttpStatusCode.TooManyRequests)
					{
						continue;
					}
					// rethrow unknown exceptions
					if (e.InnerException is not IOException)
					{
						throw;
					}

					lastException = e;
				}
			}

			if (response == null)
			{
				throw new Exception("Ref response never set", lastException);
			}

			string body = await response.Content.ReadAsStringAsync(cancellationToken);
			if (cancellationToken.IsCancellationRequested)
			{
				yield break;
			}

			if (response.StatusCode == HttpStatusCode.BadRequest)
			{
				ProblemDetails? problemDetails = JsonSerializer.Deserialize<ProblemDetails>(body, DefaultSerializerSettings);
				if (problemDetails == null)
				{
					throw new Exception($"Unknown bad request body when reading incremental replication log. Body: {body}");
				}

				if (problemDetails.Type == ProblemTypes.UseSnapshot)
				{
					ProblemDetailsWithSnapshots? problemDetailsWithSnapshots = JsonSerializer.Deserialize<ProblemDetailsWithSnapshots>(body, DefaultSerializerSettings);

					if (problemDetailsWithSnapshots == null)
					{
						throw new Exception($"Unable to cast the problem details to a snapshot version. Body: {body}");
					}

					BlobId snapshotBlob = problemDetailsWithSnapshots.SnapshotId;
					NamespaceId? blobNamespace = problemDetailsWithSnapshots.BlobNamespace;
					throw new UseSnapshotException(snapshotBlob, blobNamespace!.Value);
				}

				if (problemDetails.Type == ProblemTypes.NoDataFound)
				{
					throw new ToOldTimeBucketException(refBucket);
				}
				throw new Exception($"Unknown bad request response. Body: {body}");
			}
			else if (response.StatusCode == HttpStatusCode.NotFound)
			{
				throw new NamespaceNotFoundException(ns);
			}

			response.EnsureSuccessStatusCode();
			BlobReplicationLogEvents? replicationLogEvents = JsonSerializer.Deserialize<BlobReplicationLogEvents>(body, DefaultSerializerSettings);
			if (replicationLogEvents == null)
			{
				throw new Exception($"Unknown error when deserializing replication log events {ns} {refBucket}");
			}

			if (replicationLogEvents.Events == null)
			{
				throw new Exception($"Unknown error when deserializing replication log events {ns} {refBucket} as events were empty. Body was: {body}");
			}

			foreach (BlobReplicationLogEvent logEvent in replicationLogEvents.Events)
			{
				if (cancellationToken.IsCancellationRequested)
				{
					yield break;
				}

				yield return logEvent;
			}
		}

		private static IEnumerable<string> GetRefBuckets(string bucket, CancellationToken cancellationToken)
		{
			DateTime bucketTimestamp = bucket.FromReplicationBucketIdentifier();

			while (bucketTimestamp < DateTime.UtcNow)
			{
				if (cancellationToken.IsCancellationRequested)
				{
					yield break;
				}

				bucketTimestamp = bucketTimestamp.AddMinutes(5.0);
				if (bucketTimestamp > DateTime.UtcNow)
				{
					yield break;
				}

				yield return bucketTimestamp.ToReplicationBucketIdentifier();
			}
		}

		private async Task<bool> ReplicateBlobAsync(NamespaceId ns, BlobId blob, BucketId? bucketHint, CancellationToken cancellationToken)
		{
			using TelemetrySpan scope = _tracer.StartActiveSpan("replicator.replicate_blob")
				.SetAttribute("operation.name", "replicator.replicate_blob")
				.SetAttribute("resource.name", $"{ns}.{blob}");

			bool exists = await _blobService.ExistsAsync(ns, blob, storageLayers: null, cancellationToken);
			if (exists)
			{
				_logger.LogDebug("Not replicating blob {Blob} in {Namespace} as it already existed.", blob, ns);
				return false;
			}

			_logger.LogDebug("Attempting to replicate blob {Blob} in {Namespace}.", blob, ns);

			const int RetryAttempts = 5;
			HttpResponseMessage? blobResponse = null;
			Exception? lastException = null;
			for (int i = 0; i < RetryAttempts; i++)
			{
				using HttpRequestMessage blobRequest = await BuildHttpRequestAsync(HttpMethod.Get, new Uri($"api/v1/blobs/{ns}/{blob}", UriKind.Relative));
				try
				{
					blobResponse = await _httpClient.SendAsync(blobRequest, HttpCompletionOption.ResponseHeadersRead, cancellationToken);
					break;
				}
				catch (HttpRequestException e)
				{
					blobResponse = null;
					// rethrow unknown exceptions
					if (e.InnerException is not IOException)
					{
						throw;
					}

					lastException = e;

					if (e.StatusCode == HttpStatusCode.NotFound)
					{
						// delay the retry with a second if blob is missing, as its really not expected for it to actually miss, likely we just haven't reached consistency on the fact that it does exist
						await Task.Delay(1000, cancellationToken);
					}
				}
			}

			if (blobResponse == null)
			{
				throw new Exception("Blob response never set", lastException);
			}

			if (blobResponse.StatusCode == HttpStatusCode.NotFound)
			{
				_logger.LogError("Failed to replicate {Blob} in {Namespace} due to it not existing.", blob, ns);
				return false;
			}

			if (blobResponse.StatusCode != HttpStatusCode.OK)
			{
				_logger.LogError("Bad http response when replicating {Blob} in {Namespace} . Status code: {StatusCode}", blob, ns, blobResponse.StatusCode);
				return false;
			}

			await using Stream s = await blobResponse.Content.ReadAsStreamAsync(cancellationToken);
			long? contentLength = blobResponse.Content.Headers.ContentLength;

			if (contentLength == null)
			{
				throw new Exception("Expected content-length on blob response");
			}

			using IBufferedPayload payload = await _bufferedPayloadFactory.CreateFromStreamAsync(s, contentLength.Value, cancellationToken);

			await _blobService.PutObjectAsync(ns, payload, blob, bucketHint: bucketHint, cancellationToken);

			return true;
		}

		private async Task AddToBlobReplicationLogAsync(NamespaceId ns, BlobId blob, BucketId? bucketHint)
		{
			await _replicationLog.InsertAddBlobEventAsync(ns, blob, bucketHint: bucketHint);
		}

		public void SetReplicationOffset(long? state)
		{
			if (!state.HasValue)
			{
				return;
			}

			DateTime bucketTime = DateTime.FromFileTimeUtc(state.Value);

			SetRefState(bucketTime.ToReplicationBucket().ToReplicationBucketIdentifier());
			_logger.LogWarning("Replication bucket set to {ReplicationBucket}", state.Value);
		}

		public async Task StopReplicatingAsync()
		{
			if (_disposed)
			{
				return;
			}

			await _replicationTokenSource.CancelAsync();
			await _replicationFinishedEvent.WaitOneAsync();
		}

		public ReplicatorState State => _replicationState;

		public ReplicatorInfo Info { get; private set; }

		public Task DeleteStateAsync()
		{
			_replicationState = new BlobsState();
			return SaveStateAsync(_replicationState);
		}

		public void SetRefState(string lastBucket)
		{
			_replicationState = new BlobsState() { LastBucket = lastBucket };
			_replicationLog.UpdateReplicatorStateAsync(_namespace, _name, _replicationState).Wait();
		}
	}

	public class ToOldTimeBucketException : Exception
	{
		public string TimeBucket { get; }

		public ToOldTimeBucketException(string timeBucket)
		{
			TimeBucket = timeBucket;
		}
	}

	public class BlobsState : ReplicatorState
	{
		public BlobsState()
		{
			ReplicatingGeneration = null;
			ReplicatorOffset = 0;

			LastEvent = null;
			LastBucket = null;
		}
	}
}
