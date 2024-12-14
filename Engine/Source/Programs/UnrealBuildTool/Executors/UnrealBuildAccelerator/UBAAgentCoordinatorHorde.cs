// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Net.Http;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;
using EpicGames.Horde;
using EpicGames.Horde.Agents;
using EpicGames.Horde.Common;
using EpicGames.Horde.Compute;
using EpicGames.Horde.Compute.Clients;
using EpicGames.Horde.Server;
using EpicGames.Horde.Storage;
using EpicGames.Horde.Storage.Bundles;
using EpicGames.Horde.Storage.Nodes;
using EpicGames.OIDC;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using UnrealBuildBase;

namespace UnrealBuildTool
{
	// statusRow, statusColumn, statusText, statusType, statusLink
	using StatusUpdateAction = Action<uint, uint, string, EpicGames.UBA.LogEntryType, string?>;

	class UBAHordeSession : IAsyncDisposable
	{
		/// <summary>
		/// ID for this Horde session
		/// </summary>
		readonly Guid _id = Guid.NewGuid();

		const string ResourceLogicalCores = "LogicalCores";
		ClusterId? _clusterId;

		readonly string? _pool;
		readonly bool _allowWine;
		readonly int _maxCores;
		readonly bool _strict;
		readonly ConnectionMode? _connectionMode;
		readonly Encryption? _encryption;
		readonly CancellationTokenSource _cancellationTokenSource = new();
		readonly ILogger _logger;

		readonly UBAExecutor _owner;
		internal StatusUpdateAction? _updateStatus;

		readonly BundleStorageNamespace _storage = BundleStorageNamespace.CreateInMemory(NullLogger.Instance);
		BlobLocator _ubaAgentLocator;

		readonly ServiceProvider _serviceProvider;
		readonly string _crypto;
		IComputeClient? _client;
		HordeHttpClient? _hordeHttpClient;

		public class Worker
		{
			public Stopwatch? StartTime { get; init; }
			public string? Ip { get; init; }
			public ConnectionMetadataPort? Port { get; init; }
			public ConnectionMetadataPort? ProxyPort { get; init; }

			public int NumLogicalCores { get; set; }
			public bool Active { get; set; }
			public Task? BackgroundTask { get; set; }
		}

		readonly List<Worker> _workers = new();

		public UBAHordeSession(UBAExecutor owner, Uri? serverUrl, string? accessToken, string? pool, bool allowWine, int maxCores, bool strict, ConnectionMode? connectionMode, Encryption? encryption, ILogger logger)
		{
			_owner = owner;
			_pool = pool;
			_allowWine = allowWine;
			_maxCores = maxCores;
			_strict = strict;
			_connectionMode = connectionMode;
			_encryption = encryption;
			_logger = logger;
			_crypto = owner.Crypto;

			ServiceCollection services = new();
			services.AddLogging(builder => builder.AddEpicDefault());
			services.AddHorde(options =>
			{
				options.ServerUrl = serverUrl;
				options.AccessToken = accessToken;
			});
			_serviceProvider = services.BuildServiceProvider();

			if (connectionMode == ConnectionMode.Relay && String.IsNullOrEmpty(_crypto))
			{
				_crypto = UBAExecutor.CreateCrypto();
				_encryption = Encryption.Ssl;
			}
		}

		public async ValueTask DisposeAsync()
		{
			if (_workers.Count > 0) // Should handle double-dispose, prevent cancelling twice
			{
#if NET8_0_OR_GREATER
				await _cancellationTokenSource.CancelAsync();
#else
				_cancellationTokenSource.Cancel();
#endif

				for (int idx = _workers.Count - 1; idx >= 0; idx--)
				{
					await _workers[idx].BackgroundTask!;
					_workers.RemoveAt(idx);
				}
			}

			if (_client != null)
			{
				IComputeClient client = _client;
				_client = null;
				// Set CPU resource need to zero (will also expire on the server if not updated)
				await UpdateCpuCoreNeedAsync(0);
			}

			await _serviceProvider.DisposeAsync();
			_cancellationTokenSource.Dispose();
		}

		public async Task InitAsync(bool useSentry, CancellationToken cancellationToken)
		{
			if (_client != null)
			{
				throw new InvalidOperationException("Session has already been initialized");
			}

			_client = _serviceProvider.GetRequiredService<IHordeClient>().Compute;
			_hordeHttpClient = _serviceProvider.GetRequiredService<IHordeClient>().CreateHttpClient();
			GetServerInfoResponse serverInfo = await _hordeHttpClient.GetServerInfoAsync(cancellationToken);
			_logger.LogInformation("Horde server: {ServerVersion}, agent: {AgentVersion}", serverInfo.ServerVersion, serverInfo.AgentVersion);

			DirectoryReference ubaDir = UBAExecutor.UbaBinariesDir;
			List<string> agentFiles = new();
			if (OperatingSystem.IsWindows())
			{
				agentFiles.Add("UbaAgent.exe");
				if (useSentry)
				{
					agentFiles.Add("crashpad_handler.exe");
					agentFiles.Add("sentry.dll");
				}
			}
			else if (OperatingSystem.IsLinux())
			{
				agentFiles.Add("UbaAgent");
				bool bIsDebug = false;
				if (bIsDebug)
				{
					agentFiles.Add("UbaAgent.debug");
					agentFiles.Add("UbaAgent.sym");
					agentFiles.Add("libclang_rt.tsan.so"); // Needs to be copied from autosdk
					agentFiles.Add("llvm-symbolizer"); // Needs to be copied from autosdk
				}
			}
			else if (OperatingSystem.IsMacOS())
			{
				agentFiles.Add("UbaAgent");
			}
			else
			{
				throw new PlatformNotSupportedException();
			}
			_ubaAgentLocator = await CreateToolAsync(ubaDir, agentFiles.Select(x => FileReference.Combine(ubaDir, x)), cancellationToken);
			_logger.LogInformation("Created tool bundle with locator {UbaAgentLocator}", _ubaAgentLocator.ToString());
		}

		async Task<BlobLocator> CreateToolAsync(DirectoryReference baseDir, IEnumerable<FileReference> files, CancellationToken cancellationToken)
		{
			// TODO: should drive this off API version reported by server
			BlobSerializerOptions serializerOptions = BlobSerializerOptions.Create(HordeApiVersion.Initial);

			await using IBlobWriter writer = _storage.CreateBlobWriter(serializerOptions: serializerOptions);
			DirectoryNode sandbox = new();
			await sandbox.AddFilesAsync(baseDir, files, writer, cancellationToken: cancellationToken);
			IHashedBlobRef<DirectoryNode> handle = await writer.WriteBlobAsync(sandbox, cancellationToken);
			await writer.FlushAsync(cancellationToken);
			return handle.GetLocator();
		}

		public async void RemoveCompleteWorkersAsync()
		{
			for (int idx = 0; idx < _workers.Count; idx++)
			{
				Worker worker = _workers[idx];
				if (worker.BackgroundTask!.IsCompleted)
				{
					await worker.BackgroundTask;
					lock (_workers)
						_workers.RemoveAt(idx--);
				}
			}
		}

		public void GetCoreCount(out int active, out int queued, out int activeAgents, out int queuedAgents)
		{
			active = 0;
			queued = 0;
			activeAgents = 0;
			queuedAgents = 0;
			lock (_workers)
			{
				foreach (Worker worker in _workers)
				{
					if (worker.NumLogicalCores == 0)
					{
						continue;
					}
					if (worker.Active)
					{
						++activeAgents;
						active += worker.NumLogicalCores;
					}
					else
					{
						++queuedAgents;
						queued += worker.NumLogicalCores;
					}
				}
			}
		}

		int _workerId = 0;

		static readonly HashSet<StringView> s_logProperties = new()
			{
				"ComputeIp",
				"CPU",
				"RAM",
				"DiskFreeSpace",
				"PhysicalCores",
				"LogicalCores"
			};

		public async Task<bool> AddWorkerAsync(Requirements requirements, UnrealBuildAcceleratorHordeConfig hordeConfig, CancellationToken cancellationToken, int activeCores)
		{
			if (_client == null)
			{
				throw new InvalidOperationException($"Session not initialized. Call {nameof(InitAsync)} first");
			}

			PrefixLogger workerLogger = new($"[Worker{_workerId}]", _logger);

			const string UbaPortName = "UbaPort";
			const string UbaProxyPortName = "UbaProxyPort";
			const int UbaPort = 7001;
			const int UbaProxyPort = 7002;

			// Request ID that is unique per attempt to acquire the same compute lease/worker
			// Primarily for tracking worker demand on Horde server as UBAExecutor will repeatedly try adding a new worker
			string requestId = $"{_id}-worker-{_workerId}";
			IComputeLease? lease = null;
			try
			{
				Stopwatch stopwatch = Stopwatch.StartNew();
				ConnectionMetadataRequest cmr = new()
				{
					ModePreference = _connectionMode,
					Encryption = _encryption,
					Ports = { { UbaPortName, UbaPort }, { UbaProxyPortName, UbaProxyPort } }
				};
				
				await ResolveClusterIdAsync(hordeConfig, requirements, requestId, cmr, workerLogger, cancellationToken);
				lease = await _client.TryAssignWorkerAsync(_clusterId, requirements, requestId, cmr, workerLogger, cancellationToken);
				if (lease == null)
				{
					_logger.LogDebug("Unable to assign a remote worker");

					int missingNumCores = Math.Max(0, _maxCores - activeCores);
					await UpdateCpuCoreNeedAsync(missingNumCores, cancellationToken);
					return false;
				}

				_workerId++;

				workerLogger.LogDebug("Agent properties:");

				int numLogicalCores = 24; // Assume 24 if something goes wrong here and property is not found
				string computeIp = String.Empty;
				foreach (string property in lease.Properties)
				{
					int equalsIdx = property.IndexOf('=', StringComparison.OrdinalIgnoreCase);
					StringView propertyName = new(property, 0, equalsIdx);
					if (s_logProperties.Contains(propertyName))
					{
						_logger.LogDebug("  {Property}", property);

						if (propertyName == ResourceLogicalCores && Int32.TryParse(property.AsSpan(equalsIdx + 1), out int value))
						{
							numLogicalCores = value;
						}
						else if (propertyName == "ComputeIp")
						{
							computeIp = property[(equalsIdx + 1)..];
						}
					}
				}

				// When using relay connection mode, the IP will be relay server's IP
				string ip = String.IsNullOrEmpty(lease.Ip) ? computeIp : lease.Ip;

				if (!lease.Ports.TryGetValue(UbaPortName, out ConnectionMetadataPort? ubaPort))
				{
					ubaPort = new ConnectionMetadataPort(UbaPort, UbaPort);
				}

				if (!lease.Ports.TryGetValue(UbaProxyPortName, out ConnectionMetadataPort? ubaProxyPort))
				{
					ubaProxyPort = new ConnectionMetadataPort(UbaProxyPort, UbaProxyPort);
				}

				string exeName = OperatingSystem.IsWindows() ? "UbaAgent.exe" : "UbaAgent";
				BlobLocator locator = _ubaAgentLocator;
				Worker worker = new()
				{
					StartTime = stopwatch,
					NumLogicalCores = numLogicalCores,
					Ip = ip,
					Port = ubaPort,
					ProxyPort = ubaProxyPort,
				};
				worker.BackgroundTask = RunWorkerAsync(worker, lease, locator, exeName, workerLogger, hordeConfig, _cancellationTokenSource.Token);
				lock (_workers)
					_workers.Add(worker);
				UpdateHordeStatus(null);
				lease = null; // Will be disposed by RunWorkerAsync

				return true;
			}
			finally
			{
				if (lease != null)
				{
					await lease.DisposeAsync();
				}
			}
		}

		int _targetCoreCount;

		async Task UpdateCpuCoreNeedAsync(int targetCoreCount, CancellationToken cancellationToken = default)
		{
			if (_client != null && _pool != null && _clusterId != null && targetCoreCount != _targetCoreCount)
			{
				_targetCoreCount = targetCoreCount;

				_logger.LogDebug("Setting CPU core need to {TargetCoreCount}", targetCoreCount);
				Dictionary<string, int> resourceNeeds = new() { { ResourceLogicalCores, targetCoreCount } };
				try
				{
					await _client.DeclareResourceNeedsAsync(_clusterId.Value, _pool, resourceNeeds, cancellationToken);
				}
				catch (Exception e)
				{
					_logger.Log(_strict ? LogLevel.Error : LogLevel.Debug, KnownLogEvents.Systemic_Horde_Compute, e, "Failed updating resource need to {TargetCoreCount} cores", targetCoreCount);
				}
			}
		}
		
		private async Task ResolveClusterIdAsync(UnrealBuildAcceleratorHordeConfig config, Requirements requirements, string requestId, ConnectionMetadataRequest cmr, ILogger logger, CancellationToken cancellationToken = default)
		{
			if (_client == null)
			{
				throw new InvalidOperationException($"Session not initialized. Call {nameof(InitAsync)} first");
			}
			
			if (_clusterId == null)
			{
				if (config.HordeCluster == UnrealBuildAcceleratorHordeConfig.ClusterAuto)
				{
					_clusterId = await _client.GetClusterAsync(requirements, requestId, cmr, logger, cancellationToken);
				}
				else if (!String.IsNullOrEmpty(config.HordeCluster))
				{
					_clusterId = new ClusterId(config.HordeCluster);
				}
				else
				{
					_clusterId = new ClusterId(UnrealBuildAcceleratorHordeConfig.ClusterDefault);
				}
				_logger.LogInformation("Horde cluster resolved as '{ClusterId}'", _clusterId.ToString());
			}
		}

		public static async Task<UBAHordeSession?> TryCreateHordeSessionAsync(UnrealBuildAcceleratorHordeConfig hordeConfig, UBAExecutor executor, bool bStrictErrors, ILogger logger, CancellationToken cancellationToken = default)
		{
			if (hordeConfig.bDisableHorde)
			{
				logger.LogInformation("Horde disabled via command line option.");
				return null;
			}

			if (String.IsNullOrEmpty(hordeConfig.HordeServer) && HordeOptions.GetServerUrlFromEnvironment() == null && HordeOptions.GetDefaultServerUrl() == null)
			{
				logger.LogInformation("Horde disabled. Url not set.");
				return null;
			}

			Uri? server = (hordeConfig.HordeServer == null) ? null : new Uri(hordeConfig.HordeServer);
			string? token = hordeConfig.HordeToken;

			ConnectionMode? connectionMode = Enum.TryParse(hordeConfig.HordeConnectionMode, true, out ConnectionMode cm) ? cm : null;
			Encryption? encryption = Enum.TryParse(hordeConfig.HordeEncryption, true, out Encryption enc) ? enc : null;

			// Default to SSL encryption for relay mode if unset
			if (connectionMode == ConnectionMode.Relay && encryption == null)
			{
				encryption = Encryption.Ssl;
			}

			logger.LogInformation("Horde URL: {Server}, Pool: {Pool}, Cluster {Cluster}, Condition: {Condition}, Connection: {Connection} HordeEncryption: {Encryption}",
				server, hordeConfig.HordePool ?? "(none)", hordeConfig.HordeCluster ?? "(none)", hordeConfig.HordeCondition ?? "(none)", connectionMode?.ToString() ?? "(none)", encryption?.ToString() ?? "(none)");
			try
			{
				bool allowWine = hordeConfig.bHordeAllowWine && OperatingSystem.IsWindows();

				UBAHordeSession session = new(executor, server, token, hordeConfig.HordePool, allowWine, hordeConfig.HordeMaxCores, bStrictErrors, connectionMode, encryption, logger);
				await session.InitAsync(useSentry: !String.IsNullOrEmpty(hordeConfig.UBASentryUrl), cancellationToken);
				return session;
			}
			catch (TaskCanceledException)
			{
				return null;
			}
			catch (Exception ex)
			{
				logger.Log(bStrictErrors ? LogLevel.Error : LogLevel.Information, ex, "Unable to create Horde session: {Message}", ex.Message);
				return null;
			}
		}

		static async Task<string> GetOidcBearerTokenAsync(DirectoryReference? projectDir, string oidcProvider, ILogger logger, CancellationToken cancellationToken = default)
		{
			logger.LogInformation("Performing OIDC token refresh...");

			using ITokenStore tokenStore = TokenStoreFactory.CreateTokenStore();
			IConfiguration providerConfiguration = ProviderConfigurationFactory.ReadConfiguration(Unreal.EngineDirectory.ToDirectoryInfo(), projectDir?.ToDirectoryInfo());
			OidcTokenManager oidcTokenManager = OidcTokenManager.CreateTokenManager(providerConfiguration, tokenStore, new List<string>() { oidcProvider });

			OidcTokenInfo result;
			try
			{
				result = await oidcTokenManager.GetAccessToken(oidcProvider, cancellationToken);
			}
			catch (NotLoggedInException)
			{
				result = await oidcTokenManager.LoginAsync(oidcProvider, cancellationToken);
			}

			if (result.AccessToken == null)
			{
				throw new Exception($"Unable to get access token for {oidcProvider}");
			}

			logger.LogInformation("Received bearer token for {OidcProvider}", oidcProvider);
			return result.AccessToken;
		}

		async Task RunWorkerAsync(Worker self, IComputeLease lease, BlobLocator tool, string executable, ILogger logger, UnrealBuildAcceleratorHordeConfig hordeConfig, CancellationToken cancellationToken)
		{
			logger.LogDebug("Running worker task..");
			try
			{
				await using (_ = lease)
				{
					// Create a message channel on channel id 0. The Horde Agent always listens on this channel for requests.
					const int PrimaryChannelId = 0;
					using (AgentMessageChannel channel = lease.Socket.CreateAgentMessageChannel(PrimaryChannelId, 4 * 1024 * 1024))
					{
						logger.LogDebug("Waiting for attach...");

						TimeSpan attachTimeout = TimeSpan.FromSeconds(20.0);
						try
						{
							Task attachTask = channel.WaitForAttachAsync(cancellationToken).AsTask();
							await attachTask.WaitAsync(attachTimeout, cancellationToken);
						}
						catch (TimeoutException)
						{
							logger.Log(_strict ? LogLevel.Error : LogLevel.Debug, KnownLogEvents.Systemic_Horde_Compute, "Waited {Time}s on attach message. Giving up", (int)attachTimeout.TotalSeconds);
							throw;
						}

						logger.LogDebug("Uploading files...");
						await channel.UploadFilesAsync("", tool, _storage.Backend, cancellationToken);

						string hordeHost = _owner.UBAConfig.Host;
						if (!String.IsNullOrEmpty(hordeConfig.HordeHost))
						{
							hordeHost = hordeConfig.HordeHost;
						}

						bool useListen = !String.IsNullOrEmpty(hordeConfig.HordeHost);
						List<string> arguments = new();

						if (useListen)
						{
							arguments.Add($"-Host={hordeHost}:{_owner.UBAConfig.Port}");
						}
						else
						{
							arguments.Add($"-Listen={self.Port!.AgentPort}");
							arguments.Add("-ListenTimeout=10");
						}

						if (!String.IsNullOrEmpty(_crypto))
						{
							arguments.Add($"-crypto={_crypto}");
						}

						arguments.Add("-NoPoll");
						arguments.Add("-Quiet");
						if (!String.IsNullOrEmpty(hordeConfig.UBASentryUrl))
						{
							arguments.Add($"-Sentry=\"{hordeConfig.UBASentryUrl}\"");
						}
						arguments.Add("-ProxyPort=" + self.ProxyPort!.AgentPort);
						if (_owner.UBAConfig.bUseQuic)
						{
							arguments.Add("-quic");
						}
						//arguments.Add("-NoStore");
						//arguments.Add("-KillRandom"); // For debugging

						if (System.OperatingSystem.IsMacOS())
						{
							// we need to populate the cas with all known xcodes so we can serve all the ones we have installed
							string xcodeVersion = Utils.RunLocalProcessAndReturnStdOut("/bin/sh", "-c '/usr/bin/defaults read $(xcode-select -p)/../version.plist ProductBuildVersion");
							arguments.Add($"-populateCasFromXcodeVersion={xcodeVersion}");
						}

						arguments.Add("-Dir=%UE_HORDE_SHARED_DIR%\\Uba");
						arguments.Add("-Eventfile=%UE_HORDE_TERMINATION_SIGNAL_FILE%");
						arguments.Add("-MaxIdle=15");

						if (_owner.UBAConfig.bLogEnabled)
						{
							arguments.Add("-Log");
						}

						LogLevel logLevel = _owner.UBAConfig.bDetailedLog ? LogLevel.Information : LogLevel.Debug;

						logger.Log(logLevel, "Executing child process: {Executable} {Arguments}", executable, CommandLineArguments.Join(arguments));

						ExecuteProcessFlags execFlags = _allowWine ? ExecuteProcessFlags.UseWine : ExecuteProcessFlags.None;
						await using AgentManagedProcess process = await channel.ExecuteAsync(executable, arguments, null, null, execFlags, cancellationToken);
						bool shouldConnect = !useListen;
						bool isFirstRead = true;
						string? line;

						while ((line = await process.ReadLineAsync(cancellationToken)) != null)
						{
							logger.Log(logLevel, "{Line}", line);

							if (shouldConnect && line.Contains("Listening on", StringComparison.OrdinalIgnoreCase)) // This log entry means that the agent is ready for connections.
							{
								long totalMs = self.StartTime!.ElapsedMilliseconds;
								logger.LogInformation("Connecting to UbaAgent on {Ip}:{Port} (local agent port {AgentPort}) {Seconds}.{Milliseconds} seconds after assigned",
									self.Ip, self.Port!.Port, self.Port.AgentPort, totalMs / 1000, totalMs % 1000);

								if (!_owner.Server!.AddClient(self.Ip!, self.Port.Port, _crypto))
								{
									break;
								}
								shouldConnect = false;
							}

							if (isFirstRead)
							{
								isFirstRead = false;
								self.Active = true;
								UpdateHordeStatus(null);
							}
						}
						self.NumLogicalCores = 0;
						UpdateHordeStatus(null);
						logger.LogDebug("Shutting down process");
						int ExitCode = await process.WaitForExitAsync(cancellationToken);
						if (ExitCode != 0)
						{
							logger.LogInformation($"UbaAgent exited with exit code: {ExitCode}");
						}
					}

					logger.LogDebug("Closing channel");
					await lease.CloseAsync(cancellationToken);
				}
			}
			catch (TimeoutException)
			{
				self.NumLogicalCores = 0;
				UpdateHordeStatus(null);
			}
			catch (ComputeExecutionCancelledException ex) 
			{
				// Cancellations are expected to happen due to spot instance interruptions or unscheduled maintenance of agents
				self.NumLogicalCores = 0;
				UpdateHordeStatus(null);
				
				if (!cancellationToken.IsCancellationRequested)
				{
					logger.Log(_strict ? LogLevel.Information : LogLevel.Debug, KnownLogEvents.Systemic_Horde_Compute, ex, "Compute lease cancelled");
				}
			}
			catch (Exception ex)
			{
				self.NumLogicalCores = 0;
				UpdateHordeStatus(null);

				if (!cancellationToken.IsCancellationRequested)
				{
					logger.Log(_strict ? LogLevel.Error : LogLevel.Debug, KnownLogEvents.Systemic_Horde_Compute, ex, "Exception in worker task: {Ex}", ex.ToString());

					// Add additional properties to aid debugging
					logger.Log(_strict ? LogLevel.Information : LogLevel.Debug, KnownLogEvents.Systemic_Horde_Compute, ex, "UBA agent locator {UBAAgentLocator}", _ubaAgentLocator.ToString());
				}
			}
		}

		string _additionalStatus = "";
		string _lastStatus = "";

		internal void UpdateHordeStatus(string? additionalStatus)
		{
			if (additionalStatus != null)
			{
				_additionalStatus = additionalStatus;
			}

			if (_updateStatus == null)
			{
				return;
			}

			int activeCores;
			int queuedCores;
			int activeAgents;
			int queuedAgents;
			GetCoreCount(out activeCores, out queuedCores, out activeAgents, out queuedAgents);
			string status = $"Running. {activeAgents} agent{(activeAgents != 1 ? "s" : "")} ({activeCores} cores){_additionalStatus} {(queuedAgents != 0 ? $"(Preparing {queuedAgents} agent{(queuedAgents != 1 ? "s" : "")})" : "")}";
			if (_lastStatus != status)
			{
				_lastStatus = status;
				_updateStatus(0, 6, status, EpicGames.UBA.LogEntryType.Info, null);
			}
		}
	}

	class UBAAgentCoordinatorHorde : IUBAAgentCoordinator, IDisposable
	{
		public string? Server => HordeConfig.HordeServer;
		public bool Enabled => !HordeConfig.bDisableHorde;
		public string ConnectionModeString => (Enum.TryParse(HordeConfig.HordeConnectionMode, true, out ConnectionMode cm) ? cm : ConnectionMode.Direct).ToString();

		public UBAAgentCoordinatorHorde(ILogger logger, UnrealBuildAcceleratorConfig ubaConfig, CommandLineArguments? additionalArguments = null, DirectoryReference? projectDir = null)
		{
			_logger = logger;
			_ubaConfig = ubaConfig;

			ConfigCache.ReadSettings(projectDir, BuildHostPlatform.Current.Platform, HordeConfig);
			if (HordeConfig.HordeEnabled?.Equals("BuildMachineOnly", StringComparison.OrdinalIgnoreCase) == true && !Unreal.IsBuildMachine())
			{
				// reset
				HordeConfig = new();
			}
			if (Unreal.IsBuildMachine())
			{
				string? hordeUrl = System.Environment.GetEnvironmentVariable("UE_HORDE_URL");
				if (!String.IsNullOrEmpty(hordeUrl))
				{
					HordeConfig.HordeServer = hordeUrl;
				}
			}	
			if (HordeConfig.HordeEnabled?.Equals("False", StringComparison.OrdinalIgnoreCase) == true)
			{
				HordeConfig.bDisableHorde = true;
			}
			XmlConfig.ApplyTo(HordeConfig);
			additionalArguments?.ApplyTo(HordeConfig);

			// Sentry is currently unsupported for non-Windows and non-x64
			if (!OperatingSystem.IsWindows() || RuntimeInformation.ProcessArchitecture != Architecture.X64)
			{
				HordeConfig.UBASentryUrl = null;
			}
		}

		public DirectoryReference? GetUBARootDir()
		{
			DirectoryReference? hordeSharedDir = DirectoryReference.FromString(Environment.GetEnvironmentVariable("UE_HORDE_SHARED_DIR"));
			if (hordeSharedDir != null)
			{
				return DirectoryReference.Combine(hordeSharedDir, "UbaHost");
			}
			return null;
		}

		public async Task InitAsync(UBAExecutor executor)
		{
			if (_ubaConfig.bDisableRemote)
			{
				return;
			}

			_executor = executor;
			_cancellationSource = new CancellationTokenSource();
			_hordeSessionTask = UBAHordeSession.TryCreateHordeSessionAsync(HordeConfig, executor, _ubaConfig.bStrict, _logger, _cancellationSource.Token);
			await _hordeSessionTask;
			executor.AgentCoordinatorInitialized(this, _hordeSessionTask.IsCompletedSuccessfully && _hordeSessionTask.Result != null);
		}

		public void Start(ImmediateActionQueue queue, Func<LinkedAction, bool> canRunRemotely, StatusUpdateAction updateStatus)
		{
			_updateStatus = updateStatus;

			int timerPeriod = 5000;
			bool shownNoAgentsFoundMessage = false;

			UBAHordeSession? hordeSession = _hordeSessionTask?.Result;

			string? link = null;
			if (HordeConfig.HordeServer != null)
			{
				link = $"{HordeConfig.HordeServer!.TrimEnd('/')}/agents";
				if (HordeConfig.HordePool != string.Empty)
				{
					link += $"?agent={HordeConfig.HordePool}";
				}
			}
			updateStatus(0, 1, "Horde", EpicGames.UBA.LogEntryType.Info, link);

			if (hordeSession == null)
			{
				updateStatus(0, 6, "Not created (check log)", EpicGames.UBA.LogEntryType.Info, null);
				return;
			}

			hordeSession._updateStatus = _updateStatus;
			hordeSession.UpdateHordeStatus(null);

			int requestCounter = 0;

			_timer = new(async (_) =>
			{
				_timer?.Change(Timeout.Infinite, Timeout.Infinite);

				if (_hordeSessionTask == null)
				{
					return;
				}

				UBAHordeSession? hordeSession = await _hordeSessionTask;

				if (hordeSession == null)
				{
					return;
				}

				//hordeSession.RemoveCompleteWorkersAsync();

				if (queue.IsDone || _cancellationSource!.IsCancellationRequested)
				{
					hordeSession.UpdateHordeStatus(" - Requests stopped");
					return;
				}

				// We are assuming all active logical cores are already being used.. so queueWeight is essentially work that could be executed but can't because of bandwidth
				double queueThreshold = _ubaConfig.bForceBuildAllRemote ? 0 : 5;

				try
				{
					while (true)
					{
						if (_cancellationSource!.IsCancellationRequested)
						{
							hordeSession.UpdateHordeStatus(" - Requests stopped");
							return;
						}

						hordeSession.RemoveCompleteWorkersAsync();

						double queueWeight = queue.EnumerateReadyToCompileActions().Where(x => canRunRemotely(x)).Sum(x => x.Weight);

						int activeCores;
						int queuedCores;
						int activeAgents;
						int queuedAgents;
						hordeSession.GetCoreCount(out activeCores, out queuedCores, out activeAgents, out queuedAgents);

						queueWeight -= queuedCores;

						bool isSatisfied = queueWeight <= queueThreshold || (activeCores + queuedCores) >= HordeConfig.HordeMaxCores || _cancellationSource!.IsCancellationRequested;

						hordeSession.UpdateHordeStatus(isSatisfied ? " - Requests paused" : $" - Requesting agent{("....".Substring(0, (requestCounter++ % 4)))}");

						if (isSatisfied)
						{
							break;
						}

						Requirements requirements = GetRequirements(HordeConfig);
						if (!await hordeSession.AddWorkerAsync(requirements, HordeConfig, _cancellationSource.Token, activeCores))
						{
							await Task.Delay(500); // Sleep a little bit to make sure previous horde status is visible
							string status = (queuedAgents + activeAgents) == 0 ? " - No agents available" : " - No additional agents available";
							hordeSession.UpdateHordeStatus(status);
							_logger.LogDebug(status);
							break;
						}
					}
				}
				catch (NoComputeAgentsFoundException ex)
				{
					if (!shownNoAgentsFoundMessage)
					{
						_logger.Log(_ubaConfig.bStrict ? LogLevel.Warning : LogLevel.Information, KnownLogEvents.Systemic_Horde_Compute, ex, "No agents found matching requirements (cluster: {ClusterId}, requirements: {Requirements})", ex.ClusterId, ex.Requirements);
						shownNoAgentsFoundMessage = true;
					}
				}
				catch (Exception ex)
				{
					if (!_cancellationSource!.IsCancellationRequested)
					{
						hordeSession.UpdateHordeStatus(" - " + ex.Message);
						_logger.Log(_ubaConfig.bStrict ? LogLevel.Warning : LogLevel.Information, KnownLogEvents.Systemic_Horde_Compute, ex, "Unable to get worker: {Ex}", ex.ToString());
					}
				}

				_timer?.Change(timerPeriod, Timeout.Infinite);
			}, null, HordeConfig.HordeDelay * 1000, timerPeriod);
		}

		public void Stop()
		{
			_cancellationSource?.Cancel();
		}

		public async Task CloseAsync()
		{
			_cancellationSource?.Cancel();

			if (_hordeSessionTask == null)
			{
				return;
			}

			UBAHordeSession? hordeSession = await _hordeSessionTask;
			_hordeSessionTask = null;
			if (hordeSession != null)
			{
				await hordeSession.DisposeAsync();
			}
		}

		public void Done()
		{
			if (_updateStatus != null)
			{
				_updateStatus(0, 6, "Done", EpicGames.UBA.LogEntryType.Info, null);
			}
		}

		public void Dispose()
		{
			Stop();
			CloseAsync().Wait();
			Dispose(true);
			GC.SuppressFinalize(this);
		}

		protected virtual void Dispose(bool disposing)
		{
			if (disposing)
			{
				_cancellationSource?.Dispose();
				_cancellationSource = null;
				_timer?.Dispose();
				_timer = null;
			}
		}
		
		private static Requirements GetRequirements(UnrealBuildAcceleratorHordeConfig config)
		{
			Requirements requirements = new() { Exclusive = true };
			
			if (!string.IsNullOrEmpty(config.HordeCluster))
			{
				// Apply filtering options when a cluster, either explicit or auto is specified, ensuring the cluster options take precidence.
				string condition = "";
				if (OperatingSystem.IsWindows())
				{
					condition = $"({KnownPropertyNames.OsFamily} == 'Windows' || {KnownPropertyNames.WineEnabled} == 'true')";
				}
				else if (OperatingSystem.IsMacOS())
				{
					condition = $"{KnownPropertyNames.OsFamily} == 'MacOS'";
				}
				else if (OperatingSystem.IsLinux())
				{
					condition = $"{KnownPropertyNames.OsFamily} == 'Linux'";
				}
				
				if (config.HordeCondition != null)
				{
					condition = " " + config.HordeCondition;
				}
				requirements.Condition = Condition.Parse(condition);
			}
			else
			{
				if (!String.IsNullOrEmpty(config.HordePool))
				{
					requirements.Pool = config.HordePool;
				}
				
				if (config.HordeCondition != null)
				{
					requirements.Condition = Condition.Parse(config.HordeCondition);
				}
			}
			
			return requirements;
		}

		readonly ILogger _logger;
		readonly UnrealBuildAcceleratorConfig _ubaConfig;
		UnrealBuildAcceleratorHordeConfig HordeConfig { get; init; } = new();

		UBAExecutor? _executor;
		CancellationTokenSource? _cancellationSource;
		Task<UBAHordeSession?>? _hordeSessionTask;
		StatusUpdateAction? _updateStatus;
		Timer? _timer;
	}
}
