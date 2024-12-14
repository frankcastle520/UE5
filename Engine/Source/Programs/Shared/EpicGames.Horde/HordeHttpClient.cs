// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Net;
using System.Net.Http;
using System.Net.Http.Headers;
using System.Net.Http.Json;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;
using EpicGames.Horde.Agents.Telemetry;
using EpicGames.Horde.Artifacts;
using EpicGames.Horde.Commits;
using EpicGames.Horde.Dashboard;
using EpicGames.Horde.Jobs;
using EpicGames.Horde.Jobs.Graphs;
using EpicGames.Horde.Logs;
using EpicGames.Horde.Projects;
using EpicGames.Horde.Secrets;
using EpicGames.Horde.Server;
using EpicGames.Horde.Storage;
using EpicGames.Horde.Streams;
using EpicGames.Horde.Tools;
using EpicGames.Horde.Ugs;

using static EpicGames.Horde.HordeHttpRequest;

#pragma warning disable CA2234

namespace EpicGames.Horde
{
	using JsonObject = System.Text.Json.Nodes.JsonObject;

	/// <summary>
	/// Wraps an Http client which communicates with the Horde server
	/// </summary>
	public sealed class HordeHttpClient : IDisposable
	{
		/// <summary>
		/// Name of an environment variable containing the Horde server URL
		/// </summary>
		public const string HordeUrlEnvVarName = "UE_HORDE_URL";

		/// <summary>
		/// Name of an environment variable containing a token for connecting to the Horde server
		/// </summary>
		public const string HordeTokenEnvVarName = "UE_HORDE_TOKEN";

		/// <summary>
		/// Name of clients created from the http client factory
		/// </summary>
		public const string HttpClientName = "HordeHttpClient";

		/// <summary>
		/// Name of clients used for anonymous requests.
		/// </summary>
		public const string AnonymousHttpClientName = "HordeAnonymousHttpClient";

		/// <summary>
		/// Name of clients created from the http client factory for handling upload redirects. Should not contain Horde auth headers.
		/// </summary>
		public const string UploadRedirectHttpClientName = "HordeUploadRedirectHttpClient";

		/// <summary>
		/// Accessor for the inner http client
		/// </summary>
		public HttpClient HttpClient => _httpClient;

		readonly HttpClient _httpClient;

		internal static JsonSerializerOptions JsonSerializerOptions => HordeHttpRequest.JsonSerializerOptions;

		/// <summary>
		/// Base address for the Horde server
		/// </summary>
		public Uri BaseUrl => _httpClient.BaseAddress ?? throw new InvalidOperationException("Expected Horde server base address to be configured");

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="httpClient">The inner HTTP client instance</param>
		public HordeHttpClient(HttpClient httpClient)
		{
			_httpClient = httpClient;
		}

		/// <inheritdoc/>
		public void Dispose()
		{
			_httpClient.Dispose();
		}

		/// <summary>
		/// Configures a JSON serializer to read Horde responses
		/// </summary>
		/// <param name="options">options for the serializer</param>
		public static void ConfigureJsonSerializer(JsonSerializerOptions options)
			=> HordeHttpRequest.ConfigureJsonSerializer(options);

		#region Connection
		/// <summary>
		/// Check account login status.
		/// </summary>
		public async Task<bool> CheckConnectionAsync(CancellationToken cancellationToken = default)
		{
			HttpResponseMessage response = await _httpClient.GetAsync("account", cancellationToken);

			return response.IsSuccessStatusCode;
		}

		#endregion

		#region Artifacts

		/// <summary>
		/// Creates a new artifact
		/// </summary>
		/// <param name="name">Name of the artifact</param>
		/// <param name="type">Additional search keys tagged on the artifact</param>
		/// <param name="description">Description for the artifact</param>
		/// <param name="streamId">Stream to create the artifact for</param>
		/// <param name="commitId">Commit for the artifact</param>
		/// <param name="keys">Keys used to identify the artifact</param>
		/// <param name="metadata">Metadata for the artifact</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		public Task<CreateArtifactResponse> CreateArtifactAsync(ArtifactName name, ArtifactType type, string? description, StreamId streamId, CommitId commitId, IEnumerable<string>? keys = null, IEnumerable<string>? metadata = null, CancellationToken cancellationToken = default)
		{
			return PostAsync<CreateArtifactResponse, CreateArtifactRequest>(_httpClient, $"api/v2/artifacts", new CreateArtifactRequest(name, type, description, streamId, keys?.ToList() ?? new List<string>(), metadata?.ToList() ?? new List<string>()) { CommitId = commitId }, cancellationToken);
		}

		/// <summary>
		/// Deletes an artifact
		/// </summary>
		/// <param name="id">Identifier for the artifact</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		public async Task DeleteArtifactAsync(ArtifactId id, CancellationToken cancellationToken = default)
		{
			await DeleteAsync(_httpClient, $"api/v2/artifacts/{id}", cancellationToken);
		}

		/// <summary>
		/// Gets metadata about an artifact object
		/// </summary>
		/// <param name="id">Identifier for the artifact</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		public Task<GetArtifactResponse> GetArtifactAsync(ArtifactId id, CancellationToken cancellationToken = default)
		{
			return GetAsync<GetArtifactResponse>(_httpClient, $"api/v2/artifacts/{id}", cancellationToken);
		}

		/// <summary>
		/// Gets a zip stream for a particular artifact
		/// </summary>
		/// <param name="id">Identifier for the artifact</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		public async Task<Stream> GetArtifactZipAsync(ArtifactId id, CancellationToken cancellationToken = default)
		{
			return await _httpClient.GetStreamAsync($"api/v2/artifacts/{id}/zip", cancellationToken);
		}

		/// <summary>
		/// Finds artifacts with a certain type with an optional streamId
		/// </summary>
		/// <param name="streamId">Stream to look for the artifact in</param>
		/// <param name="minCommitId">The minimum change number for the artifacts</param>
		/// <param name="maxCommitId">The minimum change number for the artifacts</param>
		/// <param name="name">Name of the artifact</param>
		/// <param name="type">Type to find</param>
		/// <param name="keys">Keys for artifacts to return</param>
		/// <param name="maxResults">Maximum number of results to return</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>Information about all the artifacts</returns>
		public async Task<List<GetArtifactResponse>> FindArtifactsAsync(StreamId? streamId = null, CommitId? minCommitId = null, CommitId? maxCommitId = null, ArtifactName? name = null, ArtifactType? type = null, IEnumerable<string>? keys = null, int maxResults = 100, CancellationToken cancellationToken = default)
		{
			QueryStringBuilder queryParams = new QueryStringBuilder();

			if (streamId != null)
			{
				queryParams.Add("streamId", streamId.ToString()!);
			}

			if (minCommitId != null)
			{
				queryParams.Add("minChange", minCommitId.ToString()!);
			}

			if (maxCommitId != null)
			{
				queryParams.Add("maxChange", maxCommitId.ToString()!);
			}

			if (name != null)
			{
				queryParams.Add("name", name.Value.ToString());
			}

			if (type != null)
			{
				queryParams.Add("type", type.Value.ToString());
			}

			if (keys != null)
			{
				foreach (string key in keys)
				{
					queryParams.Add("key", key);
				}
			}

			queryParams.Add("maxResults", maxResults.ToString());

			FindArtifactsResponse response = await GetAsync<FindArtifactsResponse>(_httpClient, $"api/v2/artifacts?{queryParams}", cancellationToken);
			return response.Artifacts;
		}

		#endregion

		#region Dashboard

		/// <summary>
		/// Create a new dashboard preview item
		/// </summary>
		/// <param name="request">Request to create a new preview item</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>Config information needed by the dashboard</returns>
		public Task<GetDashboardPreviewResponse> CreateDashbordPreviewAsync(CreateDashboardPreviewRequest request, CancellationToken cancellationToken = default)
		{
			return PostAsync<GetDashboardPreviewResponse, CreateDashboardPreviewRequest>(_httpClient, $"api/v1/dashboard/preview", request, cancellationToken);
		}

		/// <summary>
		/// Update a dashboard preview item
		/// </summary>
		/// <returns>Config information needed by the dashboard</returns>
		public Task<GetDashboardPreviewResponse> UpdateDashbordPreviewAsync(UpdateDashboardPreviewRequest request, CancellationToken cancellationToken = default)
		{
			return PutAsync<GetDashboardPreviewResponse, UpdateDashboardPreviewRequest>(_httpClient, $"api/v1/dashboard/preview", request, cancellationToken);
		}

		/// <summary>
		/// Query dashboard preview items
		/// </summary>
		/// <returns>Config information needed by the dashboard</returns>
		public Task<List<GetDashboardPreviewResponse>> GetDashbordPreviewsAsync(bool open = true, CancellationToken cancellationToken = default)
		{
			return GetAsync<List<GetDashboardPreviewResponse>>(_httpClient, $"api/v1/dashboard/preview?open={open}", cancellationToken);
		}

		#endregion

		#region Parameters

		/// <summary>
		/// Query parameters for other tools
		/// </summary>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>Parameters for other tools</returns>
		public Task<JsonObject> GetParametersAsync(CancellationToken cancellationToken = default)
		{
			return GetParametersAsync(null, cancellationToken);
		}

		/// <summary>
		/// Query parameters for other tools
		/// </summary>
		/// <param name="path">Path for properties to return</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>Information about all the projects</returns>
		public Task<JsonObject> GetParametersAsync(string? path, CancellationToken cancellationToken = default)
		{
			string url = "api/v1/parameters";
			if (!String.IsNullOrEmpty(path))
			{
				url = $"{url}/{path}";
			}
			return GetAsync<JsonObject>(_httpClient, url, cancellationToken);
		}

		#endregion

		#region Projects

		/// <summary>
		/// Query all the projects
		/// </summary>
		/// <param name="includeStreams">Whether to include streams in the response</param>
		/// <param name="includeCategories">Whether to include categories in the response</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>Information about all the projects</returns>
		public Task<List<GetProjectResponse>> GetProjectsAsync(bool includeStreams = false, bool includeCategories = false, CancellationToken cancellationToken = default)
		{
			return GetAsync<List<GetProjectResponse>>(_httpClient, $"api/v1/projects?includeStreams={includeStreams}&includeCategories={includeCategories}", cancellationToken);
		}

		/// <summary>
		/// Retrieve information about a specific project
		/// </summary>
		/// <param name="projectId">Id of the project to get information about</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>Information about the requested project</returns>
		public Task<GetProjectResponse> GetProjectAsync(ProjectId projectId, CancellationToken cancellationToken = default)
		{
			return GetAsync<GetProjectResponse>(_httpClient, $"api/v1/projects/{projectId}", cancellationToken);
		}

		#endregion

		#region Secrets

		/// <summary>
		/// Query all the secrets available to the current user
		/// </summary>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>Information about all the projects</returns>
		public Task<GetSecretsResponse> GetSecretsAsync(CancellationToken cancellationToken = default)
		{
			return GetAsync<GetSecretsResponse>(_httpClient, $"api/v1/secrets", cancellationToken);
		}

		/// <summary>
		/// Retrieve information about a specific project
		/// </summary>
		/// <param name="secretId">Id of the secret to retrieve</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>Information about the requested project</returns>
		public Task<GetSecretResponse> GetSecretAsync(SecretId secretId, CancellationToken cancellationToken = default)
		{
			return GetAsync<GetSecretResponse>(_httpClient, $"api/v1/secrets/{secretId}", cancellationToken);
		}

		#endregion

		#region Server

		/// <summary>
		/// Gets information about the currently deployed server version
		/// </summary>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>Information about the deployed server instance</returns>
		public Task<GetServerInfoResponse> GetServerInfoAsync(CancellationToken cancellationToken = default)
		{
			return GetAsync<GetServerInfoResponse>(_httpClient, "api/v1/server/info", cancellationToken);
		}

		#endregion

		#region Storage

		/// <summary>
		/// Attempts to read a named storage ref from the server
		/// </summary>
		/// <param name="path">Path to the ref</param>
		/// <param name="cacheTime">Max allowed age for a cached value to be returned</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		public async Task<ReadRefResponse?> TryReadRefAsync(string path, RefCacheTime cacheTime = default, CancellationToken cancellationToken = default)
		{
			using (HttpRequestMessage request = new HttpRequestMessage(HttpMethod.Get, path))
			{
				if (cacheTime.IsSet())
				{
					request.Headers.CacheControl = new CacheControlHeaderValue { MaxAge = cacheTime.MaxAge };
				}

				using (HttpResponseMessage response = await _httpClient.SendAsync(request, cancellationToken))
				{
					if (response.StatusCode == HttpStatusCode.NotFound)
					{
						return null;
					}
					else if (!response.IsSuccessStatusCode)
					{
						throw new StorageException($"Unable to read ref '{path}' (status: {response.StatusCode}, body: {await response.Content.ReadAsStringAsync(cancellationToken)}");
					}
					else
					{
						return await response.Content.ReadFromJsonAsync<ReadRefResponse>(cancellationToken: cancellationToken);
					}
				}
			}
		}

		#endregion

		#region Telemetry

		/// <summary>
		/// Gets telemetry for Horde within a given range
		/// </summary>
		/// <param name="endDate">End date for the range</param>
		/// <param name="range">Number of hours to return</param>
		/// <param name="tzOffset">Timezone offset</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		public Task<List<GetUtilizationDataResponse>> GetTelemetryAsync(DateTime endDate, int range, int? tzOffset = null, CancellationToken cancellationToken = default)
		{
			QueryStringBuilder queryParams = new QueryStringBuilder();
			queryParams.Add("Range", range.ToString());
			if (tzOffset != null)
			{
				queryParams.Add("TzOffset", tzOffset.Value.ToString());
			}
			return GetAsync<List<GetUtilizationDataResponse>>(_httpClient, $"api/v1/reports/utilization/{endDate}?{queryParams}", cancellationToken);
		}

		#endregion

		#region Tools

		/// <summary>
		/// Enumerates all the available tools.
		/// </summary>
		public Task<GetToolsSummaryResponse> GetToolsAsync(CancellationToken cancellationToken = default)
		{
			return GetAsync<GetToolsSummaryResponse>(_httpClient, "api/v1/tools", cancellationToken);
		}

		/// <summary>
		/// Gets information about a particular tool
		/// </summary>
		public Task<GetToolResponse> GetToolAsync(ToolId id, CancellationToken cancellationToken = default)
		{
			return GetAsync<GetToolResponse>(_httpClient, $"api/v1/tools/{id}", cancellationToken);
		}

		/// <summary>
		/// Gets information about a particular deployment
		/// </summary>
		public Task<GetToolDeploymentResponse> GetToolDeploymentAsync(ToolId id, ToolDeploymentId deploymentId, CancellationToken cancellationToken = default)
		{
			return GetAsync<GetToolDeploymentResponse>(_httpClient, $"api/v1/tools/{id}/deployments/{deploymentId}", cancellationToken);
		}

		/// <summary>
		/// Gets a zip stream for a particular deployment
		/// </summary>
		public async Task<Stream> GetToolDeploymentZipAsync(ToolId id, ToolDeploymentId? deploymentId, CancellationToken cancellationToken = default)
		{
			if (deploymentId == null)
			{
				return await _httpClient.GetStreamAsync($"api/v1/tools/{id}?action=zip", cancellationToken);
			}
			else
			{
				return await _httpClient.GetStreamAsync($"api/v1/tools/{id}/deployments/{deploymentId}?action=zip", cancellationToken);
			}
		}

		/// <summary>
		/// Creates a new tool deployment
		/// </summary>
		/// <param name="id">Id for the tool</param>
		/// <param name="version">Version string for the new deployment</param>
		/// <param name="duration">Duration over which to deploy the tool</param>
		/// <param name="createPaused">Whether to create the deployment, but do not start rolling it out yet</param>
		/// <param name="target">Location of a directory node describing the deployment</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		public async Task<ToolDeploymentId> CreateToolDeploymentAsync(ToolId id, string? version, double? duration, bool? createPaused, HashedBlobRefValue target, CancellationToken cancellationToken = default)
		{
			CreateToolDeploymentRequest request = new CreateToolDeploymentRequest(version ?? String.Empty, duration, createPaused, target);
			CreateToolDeploymentResponse response = await PostAsync<CreateToolDeploymentResponse, CreateToolDeploymentRequest>(_httpClient, $"api/v2/tools/{id}/deployments", request, cancellationToken);
			return response.Id;
		}

		#endregion

		#region Jobs
		/// <summary>
		/// Gets job information for given job ID. Fail response if jobID does not exist.
		/// </summary>
		/// <param name="id">Id of the job to get infomation for</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns></returns>
		public Task<GetJobResponse> GetJobAsync(JobId id, CancellationToken cancellationToken = default)
		{
			return GetAsync<GetJobResponse>(_httpClient, $"api/v1/jobs/{id}", cancellationToken);
		}
		#endregion

		#region Log
		/// <summary>
		/// Get the given log file 
		/// </summary>
		/// <param name="logId">Id of the log file to retrieve</param>
		/// <param name="searchText">Text to search for in the log</param>
		/// <param name="count">Number of lines to return (default 5)</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns></returns>
		public Task<SearchLogResponse> GetSearchLogAsync(LogId logId, string searchText, int count = 5, CancellationToken cancellationToken = default)
		{
			return GetAsync<SearchLogResponse>(_httpClient, $"/api/v1/logs/{logId}/search?Text={Uri.EscapeDataString(searchText)}&count={count}", cancellationToken);
		}

		/// <summary>
		/// Get the requested number of lines from given logFileId, starting at index
		/// </summary>
		/// <param name="logId">Id of log file to retrieve lines from</param>
		/// <param name="startIndex">Start index of lines to retrieve</param>
		/// <param name="count">Number of lines to retrieve</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns></returns>
		public Task<LogLinesResponse> GetLogLinesAsync(LogId logId, int startIndex, int count, CancellationToken cancellationToken = default)
		{
			return GetAsync<LogLinesResponse>(_httpClient, $"/api/v1/logs/{logId}/lines?index={startIndex}&count={count}", cancellationToken);
		}

		#endregion

		#region Graph

		/// <summary>
		/// Get graph of the given job
		/// </summary>
		/// <param name="jobId"></param>
		/// <param name="cancellationToken"></param>
		/// <returns>Contains buildgraph information for the job</returns>
		public Task<GetGraphResponse> GetGraphAsync(JobId jobId, CancellationToken cancellationToken = default)
		{
			return GetAsync<GetGraphResponse>(_httpClient, $"/api/v1/jobs/{jobId}/graph", cancellationToken);
		}

		#endregion

		#region UGS
		/// <summary>
		/// 
		/// </summary>
		/// <param name="streamId"></param>
		/// <param name="commitId"></param>
		/// <param name="projectId"></param>
		/// <param name="cancellationToken"></param>
		/// <returns></returns>
		public Task<GetUgsMetadataResponse> GetUgsMetadataAsync(StreamId streamId, CommitId commitId, ProjectId projectId, CancellationToken cancellationToken = default)
		{
			string perforceStream = $"//{streamId.ToString().Replace('-', '/')}";
			return GetAsync<GetUgsMetadataResponse>(_httpClient, $"/ugs/api/metadata?stream={perforceStream}&change={commitId.GetPerforceChange()}&project={projectId}", cancellationToken);
		}
		#endregion
	}
}
