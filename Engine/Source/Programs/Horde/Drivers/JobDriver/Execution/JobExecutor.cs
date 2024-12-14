// Copyright Epic Games, Inc. All Rights Reserved.

using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Security.Principal;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;
using EpicGames.Core;
using EpicGames.Horde;
using EpicGames.Horde.Agents.Leases;
using EpicGames.Horde.Artifacts;
using EpicGames.Horde.Jobs;
using EpicGames.Horde.Logs;
using EpicGames.Horde.Storage;
using EpicGames.Horde.Storage.Nodes;
using Grpc.Core;
using JobDriver.Parser;
using JobDriver.Utility;
using Horde.Common.Rpc;
using HordeCommon;
using HordeCommon.Rpc.Messages;
using HordeCommon.Rpc.Tasks;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using OpenTelemetry.Trace;
using StatusCode = Grpc.Core.StatusCode;

namespace JobDriver.Execution
{
	record class JobStepInfo
	(
		JobStepId StepId,
		LogId LogId,
		string Name,
		IReadOnlyDictionary<string, string> Credentials,
		IReadOnlyDictionary<string, string> Properties,
		IReadOnlyDictionary<string, string> EnvVars,
		bool? Warnings,
		IReadOnlyList<string> Inputs,
		IReadOnlyList<string> OutputNames,
		IList<int> PublishOutputs,
		IReadOnlyList<RpcCreateGraphArtifactRequest> Artifacts
	)
	{
		public JobStepInfo(RpcBeginStepResponse response)
			: this(JobStepId.Parse(response.StepId), LogId.Parse(response.LogId), response.Name, response.Credentials, response.Properties, response.EnvVars, response.Warnings, response.Inputs, response.OutputNames, response.PublishOutputs, response.Artifacts)
		{
		}
	}

	class JobExecutorOptions
	{
		public IHordeClient HordeClient { get; }
		public DirectoryReference WorkingDir { get; }
		public IReadOnlyList<ProcessToTerminate>? ProcessesToTerminate { get; }
		public JobId JobId { get; }
		public JobStepBatchId BatchId { get; }
		public LeaseId LeaseId { get; }
		public RpcJobOptions JobOptions { get; }

		public JobExecutorOptions(IHordeClient hordeClient, DirectoryReference workingDir, IReadOnlyList<ProcessToTerminate>? processesToTerminate, JobId jobId, JobStepBatchId batchId, LeaseId leaseId, RpcJobOptions jobOptions)
		{
			HordeClient = hordeClient;
			WorkingDir = workingDir;
			ProcessesToTerminate = processesToTerminate;
			JobId = jobId;
			BatchId = batchId;
			LeaseId = leaseId;
			JobOptions = jobOptions;
		}
	}

	abstract class JobExecutor : IDisposable
	{
		protected class ExportedNode
		{
			public string Name { get; set; } = String.Empty;
			public bool RunEarly { get; set; }
			public bool? Warnings { get; set; }
			public List<string> Inputs { get; set; } = new List<string>();
			public List<string> Outputs { get; set; } = new List<string>();
			public List<string> InputDependencies { get; set; } = new List<string>();
			public List<string> OrderDependencies { get; set; } = new List<string>();
			public Dictionary<string, string> Annotations { get; set; } = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
		}

		protected class ExportedGroup
		{
			public List<string> Types { get; set; } = new List<string>();
			public List<ExportedNode> Nodes { get; set; } = new List<ExportedNode>();
		}

		protected class ExportedAggregate
		{
			public string Name { get; set; } = String.Empty;
			public List<string> Nodes { get; set; } = new List<string>();
		}

		protected class ExportedLabel
		{
			public string? Name { get; set; }
			public string? Category { get; set; }
			public string? UgsBadge { get; set; }
			public string? UgsProject { get; set; }
			public RpcLabelChange Change { get; set; } = RpcLabelChange.Current;
			public List<string> RequiredNodes { get; set; } = new List<string>();
			public List<string> IncludedNodes { get; set; } = new List<string>();
		}

		protected class ExportedBadge
		{
			public string Name { get; set; } = String.Empty;
			public string? Project { get; set; }
			public int Change { get; set; }
			public string? Dependencies { get; set; }
		}

		protected class ExportedArtifact
		{
			public string Name { get; set; } = String.Empty;
			public string? Type { get; set; }
			public string? Description { get; set; }
			public string? BasePath { get; set; }
			public List<string> Keys { get; set; } = new List<string>();
			public List<string> Metadata { get; set; } = new List<string>();
			public string NodeName { get; set; } = String.Empty;
			public string OutputName { get; set; } = String.Empty;
		}

		protected class ExportedGraph
		{
			public List<ExportedGroup> Groups { get; set; } = new List<ExportedGroup>();
			public List<ExportedAggregate> Aggregates { get; set; } = new List<ExportedAggregate>();
			public List<ExportedLabel> Labels { get; set; } = new List<ExportedLabel>();
			public List<ExportedBadge> Badges { get; set; } = new List<ExportedBadge>();
			public List<ExportedArtifact> Artifacts { get; set; } = new List<ExportedArtifact>();
		}

		class TraceEvent
		{
			public string Name { get; set; } = "Unknown";
			public string? SpanId { get; set; }
			public string? ParentId { get; set; }
			public string? Service { get; set; }
			public string? Resource { get; set; }
			public DateTimeOffset StartTime { get; set; }
			public DateTimeOffset FinishTime { get; set; }
			public Dictionary<string, string>? Metadata { get; set; }

			[JsonIgnore]
			public int Index { get; set; }
		}

		class TraceEventList
		{
			public List<TraceEvent> Spans { get; set; } = new List<TraceEvent>();
		}

		class TraceSpan
		{
			public string? Name { get; set; }
			public string? SpanId { get; set; }
			public string? ParentId { get; set; }
			public string? Service { get; set; }
			public string? Resource { get; set; }
			public long Start { get; set; }
			public long Finish { get; set; }
			public Dictionary<string, string>? Properties { get; set; }
			public List<TraceSpan>? Children { get; set; }

			public void AddChild(TraceSpan child)
			{
				Children ??= new List<TraceSpan>();
				Children.Add(child);
			}
		}

		class TestDataItem
		{
			public string Key { get; set; } = String.Empty;
			public Dictionary<string, object> Data { get; set; } = new Dictionary<string, object>();
		}

		class TestData
		{
			public List<TestDataItem> Items { get; set; } = new List<TestDataItem>();
		}

		class ReportData
		{
			public RpcReportScope Scope { get; set; }
			public RpcReportPlacement Placement { get; set; }
			public string Name { get; set; } = String.Empty;
			public string Content { get; set; } = String.Empty;
			public string FileName { get; set; } = String.Empty;
		}

		const string ScriptArgumentPrefix = "-Script=";
		const string TargetArgumentPrefix = "-Target=";

		string PreprocessedScript => $"{EnginePath}/Saved/Horde/Preprocessed.xml";
		string PreprocessedSchema => $"{EnginePath}/Saved/Horde/Preprocessed.xsd";

		protected List<string> _targets = new List<string>();
		protected string? _scriptFileName;
		protected bool _preprocessScript;
		protected bool _savePreprocessedScript;

		protected string EnginePath => String.IsNullOrEmpty(Batch.EnginePath)? "Engine" : Batch.EnginePath;

		protected Tracer Tracer { get; }

		/// <summary>
		/// Logger for the local agent process (as opposed to job logger)
		/// </summary>
		protected ILogger Logger { get; }

		protected IHordeClient HordeClient { get; }
		protected DirectoryReference WorkingDir { get; }
		protected JobId JobId { get; }
		protected JobStepBatchId BatchId { get; }
		protected LeaseId LeaseId { get; }
		protected IReadOnlyList<ProcessToTerminate>? ProcessesToTerminate { get; }

		protected RpcBeginBatchResponse Batch { get; private set; }

		protected List<string> _additionalArguments = new List<string>();
		protected bool _allowTargetChanges;

		protected bool _compileAutomationTool = true;

		protected RpcJobOptions JobOptions { get; }

		protected JobRpc.JobRpcClient JobRpc { get; private set; }
		protected Dictionary<string, string> _remapAgentTypes = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);

		protected Dictionary<string, string> _envVars = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);

		/// <summary>
		/// How often to poll the server checking if a step has been aborted
		/// Exposed as internal to ease testing.
		/// </summary>
		internal TimeSpan _stepAbortPollInterval = TimeSpan.FromSeconds(5);

		/// <summary>
		/// How long to wait before retrying a failed step abort check request
		/// </summary>
		internal TimeSpan _stepAbortPollRetryDelay = TimeSpan.FromSeconds(30);

		protected JobExecutor(JobExecutorOptions options, Tracer tracer, ILogger logger)
		{
			HordeClient = options.HordeClient;
			WorkingDir = options.WorkingDir;
			JobId = options.JobId;
			BatchId = options.BatchId;
			LeaseId = options.LeaseId;
			ProcessesToTerminate = options.ProcessesToTerminate;

			Batch = null!; // Set in InitializeAsync()

			JobOptions = options.JobOptions;
			JobRpc = null!; // Set in InitializeAsync()

			Tracer = tracer;
			Logger = logger;
		}

		/// <inheritdoc/>
		public void Dispose()
		{
			GC.SuppressFinalize(this);
			Dispose(true);
		}

		protected virtual void Dispose(bool disposing)
		{
		}

		public async Task ExecuteAsync(ILogger logger, CancellationToken cancellationToken)
		{
			using TelemetrySpan span = Tracer.StartActiveSpan($"{nameof(JobExecutor)}.{nameof(ExecuteAsync)}");
			span.SetAttribute("horde.job.id", JobId.ToString());
			span.SetAttribute("horde.job.batch_id", BatchId.ToString());
			span.SetAttribute("horde.job.executor", JobOptions.Executor);
			span.SetAttribute("horde.lease.id", LeaseId.ToString());
			JobOptions.DecorateSpan(span);
			
			JobRpc.JobRpcClient jobRpc = await HordeClient.CreateGrpcClientAsync<JobRpc.JobRpcClient>(cancellationToken);

			// Create a storage client for this session
			logger.LogInformation("Executing jobId {JobId}, batchId {BatchId}, leaseId {LeaseId} with executor {Name}", JobId, BatchId, LeaseId, JobOptions.Executor);

			// Start executing the current batch
			RpcBeginBatchResponse batch = await jobRpc.BeginBatchAsync(new RpcBeginBatchRequest(JobId, BatchId, LeaseId), cancellationToken: cancellationToken);
			try
			{
				await ExecuteBatchAsync(batch, logger, NullLogger.Instance, cancellationToken);
			}
			catch (Exception ex)
			{
				if (cancellationToken.IsCancellationRequested && IsCancellationException(ex))
				{
					if (!HordeClient.HasValidAccessToken())
					{
						logger.LogError(ex, "Connection to the server was lost; step aborted.");
					}
					else
					{
						logger.LogInformation(ex, "Step was aborted");
					}
					throw;
				}
				else
				{
					logger.LogError(ex, "Exception while executing lease {LeaseId}: {Ex}", LeaseId, ex.Message);
				}
			}

			// If this lease was cancelled, don't bother updating the job state.
			cancellationToken.ThrowIfCancellationRequested();

			// Mark the batch as complete
			await jobRpc.FinishBatchAsync(new RpcFinishBatchRequest(JobId, BatchId, LeaseId), cancellationToken: cancellationToken);
			logger.LogInformation("Done.");
		}

		/// <summary>
		/// Executes a batch
		/// </summary>
		async Task ExecuteBatchAsync(RpcBeginBatchResponse batch, ILogger logger, ILogger localLogger, CancellationToken cancellationToken)
		{
			JobRpc.JobRpcClient rpcClient = await HordeClient.CreateGrpcClientAsync<JobRpc.JobRpcClient>(cancellationToken);

			await TerminateProcessHelper.TerminateProcessesAsync(TerminateCondition.BeforeBatch, WorkingDir, ProcessesToTerminate, logger, cancellationToken);

			// Try to initialize the executor
			logger.LogInformation("Initializing executor...");
			using (logger.BeginIndentScope("  "))
			{
				await InitializeAsync(batch, logger, cancellationToken);
			}

			try
			{
				// Execute the steps
				logger.LogInformation("Executing steps...");
				for (; ; )
				{
					// Get the next step to execute
					RpcBeginStepResponse stepResponse = await rpcClient.BeginStepAsync(new RpcBeginStepRequest(JobId, BatchId, LeaseId), cancellationToken: cancellationToken);
					if (stepResponse.State == RpcBeginStepResponse.Types.Result.Waiting)
					{
						logger.LogInformation("Waiting for dependency to be ready");
						await Task.Delay(TimeSpan.FromSeconds(20.0), cancellationToken);
						continue;
					}
					else if (stepResponse.State == RpcBeginStepResponse.Types.Result.Complete)
					{
						logger.LogInformation("No more steps to execute; finalizing lease.");
						break;
					}
					else if (stepResponse.State != RpcBeginStepResponse.Types.Result.Ready)
					{
						logger.LogError("Unexpected step state: {StepState}", stepResponse.State);
						break;
					}

					JobStepInfo step = new JobStepInfo(stepResponse);

					// Get current disk space available. This will allow us to more easily spot steps that eat up a lot of disk space.
					string? driveName;
					if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
					{
						driveName = Path.GetPathRoot(WorkingDir.FullName);
					}
					else
					{
						driveName = WorkingDir.FullName;
					}

					float availableFreeSpace = 0;
					if (driveName != null)
					{
						try
						{
							DriveInfo info = new DriveInfo(driveName);
							availableFreeSpace = (1.0f * info.AvailableFreeSpace) / 1024 / 1024 / 1024;
						}
						catch (Exception ex)
						{
							logger.LogWarning(ex, "Unable to query disk info for path '{DriveName}'", driveName);
						}
					}

					// Print the new state
					Stopwatch stepTimer = Stopwatch.StartNew();

					logger.LogInformation("Starting job {JobId}, batch {BatchId}, step {StepId} (Drive Space Left: {DriveSpaceRemaining} GB)", JobId, BatchId, step.StepId, availableFreeSpace.ToString("F1"));

					// Create a trace span
					using TelemetrySpan span = Tracer.StartActiveSpan("Execute");
					span.SetAttribute("resource.name", step.Name);
					span.SetAttribute("horde.job.step.id", step.StepId.ToString());
					span.SetAttribute("horde.job.step.name", step.Name);
					span.SetAttribute("horde.job.step.log_id", step.LogId.ToString());

					// Update the context to include information about this step
					JobStepOutcome stepOutcome;
					JobStepState stepState;
					using (logger.BeginIndentScope("  "))
					{
						// Start writing to the log file
#pragma warning disable CA2000 // Dispose objects before losing scope
						await using (JobStepLogger stepLogger = new JobStepLogger(HordeClient, step.LogId, localLogger, JobId, BatchId, step.StepId, step.Warnings, LogLevel.Debug, logger))
						{
							// Execute the task
							using CancellationTokenSource stepPollCancelSource = new CancellationTokenSource();
							using CancellationTokenSource stepAbortSource = new CancellationTokenSource();
							TaskCompletionSource<bool> stepFinishedSource = new TaskCompletionSource<bool>(TaskCreationOptions.RunContinuationsAsynchronously);
							Task stepPollTask = Task.Run(() => PollForStepAbortAsync(rpcClient, JobId, BatchId, step.StepId, stepAbortSource, stepFinishedSource.Task, logger, stepPollCancelSource.Token), cancellationToken);

							try
							{
								ILogger forwardingLogger = new DefaultLoggerIndentHandler(stepLogger);
								(stepOutcome, stepState) = await ExecuteStepAsync(step, forwardingLogger, cancellationToken, stepAbortSource.Token);
							}
							finally
							{
								// Will get called even when cancellation token for the lease/batch fires
								stepFinishedSource.SetResult(true); // Tell background poll task to stop
								await stepPollTask;
							}

							// Kill any processes spawned by the step
							await TerminateProcessHelper.TerminateProcessesAsync(TerminateCondition.AfterStep, WorkingDir, ProcessesToTerminate, logger, cancellationToken);

							// Wait for the logger to finish
							await stepLogger.StopAsync();

							// Reflect the warnings/errors in the step outcome
							if (stepOutcome > stepLogger.Outcome)
							{
								stepOutcome = stepLogger.Outcome;
							}
						}
#pragma warning restore CA2000 // Dispose objects before losing scope

						// Update the server with the outcome from the step
						logger.LogInformation("Marking step as complete (Outcome={Outcome}, State={StepState})", stepOutcome, stepState);
						await rpcClient.UpdateStepAsync(new RpcUpdateStepRequest(JobId, BatchId, step.StepId, stepState, stepOutcome), cancellationToken: cancellationToken);
					}

					// Print the finishing state
					stepTimer.Stop();
					logger.LogInformation("Completed in {Time}", stepTimer.Elapsed);
				}
			}
			catch (Exception ex)
			{
				if (cancellationToken.IsCancellationRequested && IsCancellationException(ex))
				{
					if (!HordeClient.HasValidAccessToken())
					{
						logger.LogError(ex, "Exception while executing batch: {Ex}", ex);
					}
					else
					{
						logger.LogError("Lease was aborted");
					}
				}
			}

			// Terminate any processes which are still running
			try
			{
				await TerminateProcessHelper.TerminateProcessesAsync(TerminateCondition.AfterBatch, WorkingDir, ProcessesToTerminate, logger, CancellationToken.None);
			}
			catch (Exception ex)
			{
				logger.LogWarning(ex, "Exception while terminating processes: {Message}", ex.Message);
			}

			// Clean the environment
			logger.LogInformation("Finalizing...");
			using (logger.BeginIndentScope("  "))
			{
				await FinalizeAsync(logger, CancellationToken.None);
			}
		}

		/// <summary>
		/// Executes a step
		/// </summary>
		/// <param name="step">Step to execute</param>
		/// <param name="stepLogger">Logger for the step</param>
		/// <param name="cancellationToken">Cancellation token to abort the batch</param>
		/// <param name="stepCancellationToken">Cancellation token to abort only this individual step</param>
		/// <returns>Async task</returns>
		internal async Task<(JobStepOutcome, JobStepState)> ExecuteStepAsync(JobStepInfo step, ILogger stepLogger, CancellationToken cancellationToken, CancellationToken stepCancellationToken)
		{
			using CancellationTokenSource combined = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken, stepCancellationToken);
			try
			{
				JobStepOutcome stepOutcome = await RunAsync(step, stepLogger, combined.Token);
				return (stepOutcome, JobStepState.Completed);
			}
			catch (Exception ex)
			{
				if (cancellationToken.IsCancellationRequested && IsCancellationException(ex))
				{
					stepLogger.LogError("The step was cancelled by batch/lease");
					throw;
				}

				if (stepCancellationToken.IsCancellationRequested && IsCancellationException(ex))
				{
					stepLogger.LogError("The step was intentionally cancelled");
					return (JobStepOutcome.Failure, JobStepState.Aborted);
				}

				stepLogger.LogError(ex, "Exception while executing step: {Ex}", ex);
				return (JobStepOutcome.Failure, JobStepState.Completed);
			}
		}

		/// <summary>
		/// Determine if the given exception was triggered due to a cancellation event
		/// </summary>
		/// <param name="ex">The exception to check</param>
		/// <returns>True if the exception is a cancellation exception</returns>
		static bool IsCancellationException(Exception ex)
		{
			if (ex is OperationCanceledException)
			{
				return true;
			}

			RpcException? rpcException = ex as RpcException;
			if (rpcException != null && rpcException.StatusCode == StatusCode.Cancelled)
			{
				return true;
			}

			return false;
		}

		internal async Task PollForStepAbortAsync(JobRpc.JobRpcClient rpcClient, JobId jobId, JobStepBatchId batchId, JobStepId stepId, CancellationTokenSource stepCancelSource, Task finishedTask, ILogger leaseLogger, CancellationToken cancellationToken)
		{
			while (!finishedTask.IsCompleted)
			{
				TimeSpan waitTime = _stepAbortPollInterval;
				try
				{
					RpcGetStepResponse res = await rpcClient.GetStepAsync(new RpcGetStepRequest(jobId, batchId, stepId), cancellationToken: cancellationToken);
					if (res.AbortRequested)
					{
						leaseLogger.LogDebug("Step was aborted by server (JobId={JobId} BatchId={BatchId} StepId={StepId})", jobId, batchId, stepId);
						await stepCancelSource.CancelAsync();
						break;
					}
				}
				catch (RpcException ex)
				{
					// Don't let a single RPC failure abort the running step as there can be intermittent errors on the server
					// For example temporary downtime or overload
					leaseLogger.LogError(ex, "Poll for step abort failed (JobId={JobId} BatchId={BatchId} StepId={StepId}). Retrying...", jobId, batchId, stepId);
					waitTime = _stepAbortPollRetryDelay;
				}

				await Task.WhenAny(Task.Delay(waitTime, cancellationToken), finishedTask);
			}
		}

		public virtual async Task InitializeAsync(RpcBeginBatchResponse batch, ILogger logger, CancellationToken cancellationToken)
		{
			using TelemetrySpan span = Tracer.StartActiveSpan($"{nameof(JobExecutor)}.{nameof(InitializeAsync)}");
			
			Batch = batch;
			JobRpc = await HordeClient.CreateGrpcClientAsync<JobRpc.JobRpcClient>(cancellationToken);

			_envVars[HordeHttpClient.HordeUrlEnvVarName] = HordeClient.ServerUrl.ToString();

			string? accessToken = await HordeClient.GetAccessTokenAsync(false, cancellationToken);
			if (accessToken != null)
			{
				_envVars[HordeHttpClient.HordeTokenEnvVarName] = accessToken;
			}

			// Setup the agent type
			foreach (KeyValuePair<string, string> envVar in Batch.Environment)
			{
				_envVars[envVar.Key] = envVar.Value;
			}

			// Figure out if we're running as an admin
			if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
			{
				if (IsUserAdministrator())
				{
					logger.LogInformation("Running as an elevated user.");
				}
				else
				{
					logger.LogInformation("Running as an restricted user.");
				}
			}

			// Get the BuildGraph arguments
			foreach (string argument in Batch.Arguments)
			{
				const string RemapAgentTypesPrefix = "-RemapAgentTypes=";
				if (argument.StartsWith(RemapAgentTypesPrefix, StringComparison.OrdinalIgnoreCase))
				{
					foreach (string map in argument.Substring(RemapAgentTypesPrefix.Length).Split(','))
					{
						int colonIdx = map.IndexOf(':', StringComparison.Ordinal);
						if (colonIdx != -1)
						{
							_remapAgentTypes[map.Substring(0, colonIdx)] = map.Substring(colonIdx + 1);
						}
					}
				}
				else if (argument.StartsWith(ScriptArgumentPrefix, StringComparison.OrdinalIgnoreCase))
				{
					_scriptFileName = argument.Substring(ScriptArgumentPrefix.Length);
				}
				else if (argument.Equals("-Preprocess", StringComparison.OrdinalIgnoreCase))
				{
					_preprocessScript = true;
				}
				else if (argument.Equals("-SavePreprocessed", StringComparison.OrdinalIgnoreCase))
				{
					_savePreprocessedScript = true;
				}
				else if (argument.Equals("-AllowTargetChanges", StringComparison.OrdinalIgnoreCase))
				{
					_allowTargetChanges = true;
				}
				else if (argument.StartsWith(TargetArgumentPrefix, StringComparison.OrdinalIgnoreCase))
				{
					_targets.Add(argument.Substring(TargetArgumentPrefix.Length));
				}
				else
				{
					_additionalArguments.Add(argument);
				}
			}
			if (Batch.PreflightChange != 0)
			{
				_additionalArguments.Add($"-set:PreflightChange={Batch.PreflightChange}");
			}
		}

		static bool IsUserAdministrator()
		{
			if (!OperatingSystem.IsWindows())
			{
				return false;
			}

			try
			{
				using (WindowsIdentity identity = WindowsIdentity.GetCurrent())
				{
					WindowsPrincipal principal = new WindowsPrincipal(identity);
					return principal.IsInRole(WindowsBuiltInRole.Administrator);
				}
			}
			catch
			{
				return false;
			}
		}

		public virtual async Task<JobStepOutcome> RunAsync(JobStepInfo step, ILogger logger, CancellationToken cancellationToken)
		{
			if (step.Name == "Setup Build")
			{
				if (await SetupAsync(step, logger, cancellationToken))
				{
					return JobStepOutcome.Success;
				}
				else
				{
					return JobStepOutcome.Failure;
				}
			}
			else
			{
				if (await ExecuteAsync(step, logger, cancellationToken))
				{
					return JobStepOutcome.Success;
				}
				else
				{
					return JobStepOutcome.Failure;
				}
			}
		}

		public virtual Task FinalizeAsync(ILogger jobLogger, CancellationToken cancellationToken)
		{
			using TelemetrySpan span = Tracer.StartActiveSpan($"{nameof(JobExecutor)}.{nameof(FinalizeAsync)}");
			return Task.CompletedTask;
		}

		static DirectoryReference GetAutomationToolDir(DirectoryReference sharedStorageDir)
		{
			return DirectoryReference.Combine(sharedStorageDir, "UAT");
		}

		protected void CopyAutomationTool(DirectoryReference sharedStorageDir, DirectoryReference workspaceDir, ILogger logger)
		{
			DirectoryReference buildDir = GetAutomationToolDir(sharedStorageDir);

			FileReference[] automationToolPaths = new FileReference[]
			{
				FileReference.Combine(buildDir, $"{EnginePath}/Binaries/DotNET/AutomationTool.exe"),
				FileReference.Combine(buildDir, $"{EnginePath}/Binaries/DotNET/AutomationTool/AutomationTool.exe")
			};

			if (automationToolPaths.Any(automationTool => FileReference.Exists(automationTool)))
			{
				logger.LogInformation("Copying AutomationTool binaries from '{BuildDir}' to '{WorkspaceDir}", buildDir, workspaceDir);
				foreach (FileReference sourceFile in DirectoryReference.EnumerateFiles(buildDir, "*", SearchOption.AllDirectories))
				{
					FileReference targetFile = FileReference.Combine(workspaceDir, sourceFile.MakeRelativeTo(buildDir));
					if (FileReference.Exists(targetFile))
					{
						FileUtils.ForceDeleteFile(targetFile);
					}
					DirectoryReference.CreateDirectory(targetFile.Directory);
					FileReference.Copy(sourceFile, targetFile);
				}
				_compileAutomationTool = false;
			}
		}

		protected void DeleteCachedBuildGraphManifests(DirectoryReference workspaceDir, ILogger logger)
		{
			DirectoryReference manifestDir = DirectoryReference.Combine(workspaceDir, $"{EnginePath}/Saved/BuildGraph");
			if (DirectoryReference.Exists(manifestDir))
			{
				try
				{
					FileUtils.ForceDeleteDirectoryContents(manifestDir);
				}
				catch (Exception ex)
				{
					logger.LogWarning(ex, "Unable to delete contents of {ManifestDir}", manifestDir.ToString());
				}
			}
		}

		protected abstract Task<bool> SetupAsync(JobStepInfo step, ILogger logger, CancellationToken cancellationToken);

		protected abstract Task<bool> ExecuteAsync(JobStepInfo step, ILogger logger, CancellationToken cancellationToken);

		const string SetupStepName = "setup";
		const string BuildGraphTempStorageDir = "BuildGraph";

		IStorageNamespace GetStorageNamespace(NamespaceId namespaceId, string? token)
		{
			return HordeClient.GetStorageNamespace(namespaceId, token);
		}

		protected virtual async Task<bool> SetupAsync(JobStepInfo step, DirectoryReference workspaceDir, bool? useP4, ILogger logger, CancellationToken cancellationToken)
		{
			FileReference definitionFile = FileReference.Combine(workspaceDir, $"{EnginePath}/Saved/Horde/Exported.json");

			StringBuilder arguments = new StringBuilder($"BuildGraph");
			if (_scriptFileName != null)
			{
				arguments.AppendArgument(ScriptArgumentPrefix, _scriptFileName);
			}
			arguments.AppendArgument("-HordeExport=", definitionFile.FullName);
			arguments.AppendArgument("-ListOnly");

			if (!_allowTargetChanges)
			{
				foreach (string target in _targets)
				{
					arguments.AppendArgument(TargetArgumentPrefix + target);
				}
			}

			//Arguments.AppendArgument("-TokenSignature=", JobId.ToString());
			foreach (string additionalArgument in _additionalArguments)
			{
				arguments.AppendArgument(additionalArgument);
			}

			FileReference? preprocessedScriptFile = null;
			FileReference? preprocessedSchemaFile = null;
			if (_preprocessScript || _savePreprocessedScript)
			{
				preprocessedScriptFile = FileReference.Combine(workspaceDir, PreprocessedScript);
				arguments.AppendArgument("-Preprocess=", preprocessedScriptFile.FullName);

				preprocessedSchemaFile = FileReference.Combine(workspaceDir, PreprocessedSchema);
				arguments.AppendArgument("-Schema=", preprocessedSchemaFile.FullName);
			}

			int result = await ExecuteAutomationToolAsync(step, workspaceDir, arguments.ToString(), useP4, logger, cancellationToken);
			if (result != 0)
			{
				return false;
			}

			List<FileReference> buildGraphFiles = new List<FileReference>();
			buildGraphFiles.Add(definitionFile);
			if (preprocessedScriptFile != null)
			{
				buildGraphFiles.Add(preprocessedScriptFile);
			}
			if (preprocessedSchemaFile != null)
			{
				buildGraphFiles.Add(preprocessedSchemaFile);
			}

			{
				using TelemetrySpan span = Tracer.StartActiveSpan("TempStorage");
				const string Prefix = "horde.temp_storage.";
				span.SetAttribute(Prefix + "action", "upload");
				
				// Create the artifact
				ArtifactName artifactName = TempStorage.GetArtifactNameForNode(SetupStepName);
				ArtifactType artifactType = ArtifactType.StepOutput;

				RpcCreateJobArtifactRequestV2 artifactRequest = new RpcCreateJobArtifactRequestV2();
				artifactRequest.JobId = JobId.ToString();
				artifactRequest.StepId = step.StepId.ToString();
				artifactRequest.Name = artifactName.ToString();
				artifactRequest.Type = artifactType.ToString();

				RpcCreateJobArtifactResponseV2 artifact = await JobRpc.CreateArtifactV2Async(artifactRequest, cancellationToken: cancellationToken);
				ArtifactId artifactId = ArtifactId.Parse(artifact.Id);
				logger.LogInformation("Creating output artifact {ArtifactId} '{ArtifactName}' ({ArtifactType}) with ref {RefName} in namespace {NamespaceId}", artifactId, artifactName, artifactType, artifact.RefName, artifact.NamespaceId);
				DecorateSpanWithArtifact(span, Prefix, artifactRequest, artifact);

				// Write the data
				IStorageNamespace storage = GetStorageNamespace(new NamespaceId(artifact.NamespaceId), artifact.Token);

				Stopwatch timer = Stopwatch.StartNew();

				IHashedBlobRef<DirectoryNode> rootNodeRef;
				await using (IBlobWriter blobWriter = storage.CreateBlobWriter(artifact.RefName))
				{
					DirectoryNode buildGraphNode = new DirectoryNode();
					await buildGraphNode.AddFilesAsync(workspaceDir, buildGraphFiles, blobWriter, cancellationToken: cancellationToken);
					IHashedBlobRef<DirectoryNode> outputNodeRef = await blobWriter.WriteBlobAsync(buildGraphNode, cancellationToken);

					DirectoryNode rootNode = new DirectoryNode();
					rootNode.AddDirectory(new DirectoryEntry(BuildGraphTempStorageDir, buildGraphNode.Length, outputNodeRef));

					rootNodeRef = await blobWriter.WriteBlobAsync(rootNode, cancellationToken);
				}
				await storage.WriteRefAsync(artifact.RefName, rootNodeRef, new RefOptions(), cancellationToken);

				logger.LogInformation("Upload took {Time:n1}s", timer.Elapsed.TotalSeconds);
			}

			RpcUpdateGraphRequest updateGraph = await ParseGraphUpdateAsync(definitionFile, logger, cancellationToken);
			await JobRpc.UpdateGraphAsync(updateGraph, null, null, cancellationToken);

			HashSet<string> validTargets = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
			validTargets.Add("Setup Build");
			validTargets.UnionWith(updateGraph.Groups.SelectMany(x => x.Nodes).Select(x => x.Name));
			validTargets.UnionWith(updateGraph.Aggregates.Select(x => x.Name));

			foreach (string target in _targets)
			{
				if (!validTargets.Contains(target))
				{
					logger.LogInformation("Target '{Target}' does not exist in the graph.", target);
				}
			}

			return true;
		}

		private async Task<RpcUpdateGraphRequest> ParseGraphUpdateAsync(FileReference definitionFile, ILogger logger, CancellationToken cancellationToken)
		{
			JsonSerializerOptions options = new JsonSerializerOptions();
			options.PropertyNameCaseInsensitive = true;
			options.Converters.Add(new JsonStringEnumConverter());

			ExportedGraph graph = JsonSerializer.Deserialize<ExportedGraph>(await FileReference.ReadAllBytesAsync(definitionFile, cancellationToken), options)!;

			RpcUpdateGraphRequest updateGraph = new RpcUpdateGraphRequest();
			updateGraph.JobId = JobId.ToString();

			List<string> missingAgentTypes = new List<string>();
			foreach (ExportedGroup exportedGroup in graph.Groups)
			{
				string? agentTypeName = null;
				foreach (string validAgentTypeName in exportedGroup.Types)
				{
					string? thisAgentTypeName;
					if (!_remapAgentTypes.TryGetValue(validAgentTypeName, out thisAgentTypeName))
					{
						thisAgentTypeName = validAgentTypeName;
					}

					if (Batch.ValidAgentTypes.Contains(thisAgentTypeName))
					{
						agentTypeName = thisAgentTypeName;
						break;
					}
				}

				if (agentTypeName == null)
				{
					agentTypeName = exportedGroup.Types.FirstOrDefault() ?? "Unspecified";
					foreach (ExportedNode node in exportedGroup.Nodes)
					{
						missingAgentTypes.Add($"  {node.Name} ({String.Join(", ", exportedGroup.Types)})");
					}
				}

				RpcCreateGroupRequest createGroup = new RpcCreateGroupRequest();
				createGroup.AgentType = agentTypeName;

				foreach (ExportedNode exportedNode in exportedGroup.Nodes)
				{
					RpcCreateNodeRequest createNode = new RpcCreateNodeRequest();
					createNode.Name = exportedNode.Name;
					if (exportedNode.Inputs != null)
					{
						createNode.Inputs.Add(exportedNode.Inputs);
					}
					if (exportedNode.Outputs != null)
					{
						createNode.Outputs.Add(exportedNode.Outputs);
					}
					if (exportedNode.InputDependencies != null)
					{
						createNode.InputDependencies.Add(exportedNode.InputDependencies);
					}
					if (exportedNode.OrderDependencies != null)
					{
						createNode.OrderDependencies.Add(exportedNode.OrderDependencies);
					}
					createNode.RunEarly = exportedNode.RunEarly;
					createNode.Warnings = exportedNode.Warnings;
					createNode.Priority = (int)Priority.Normal;
					createNode.Annotations.Add(exportedNode.Annotations);
					createGroup.Nodes.Add(createNode);
				}
				updateGraph.Groups.Add(createGroup);
			}

			if (missingAgentTypes.Count > 0)
			{
				logger.LogInformation("The following nodes cannot be executed in this stream due to missing agent types:");
				foreach (string missingAgentType in missingAgentTypes)
				{
					logger.LogInformation("{Node}", missingAgentType);
				}
			}

			foreach (ExportedAggregate exportedAggregate in graph.Aggregates)
			{
				RpcCreateAggregateRequest createAggregate = new RpcCreateAggregateRequest();
				createAggregate.Name = exportedAggregate.Name;
				createAggregate.Nodes.AddRange(exportedAggregate.Nodes);
				updateGraph.Aggregates.Add(createAggregate);
			}

			foreach (ExportedLabel exportedLabel in graph.Labels)
			{
				RpcCreateLabelRequest createLabel = new RpcCreateLabelRequest();
				if (exportedLabel.Name != null)
				{
					createLabel.DashboardName = exportedLabel.Name;
				}
				if (exportedLabel.Category != null)
				{
					createLabel.DashboardCategory = exportedLabel.Category;
				}
				if (exportedLabel.UgsBadge != null)
				{
					createLabel.UgsName = exportedLabel.UgsBadge;
				}
				if (exportedLabel.UgsProject != null)
				{
					createLabel.UgsProject = exportedLabel.UgsProject;
				}
				createLabel.Change = exportedLabel.Change;
				createLabel.RequiredNodes.AddRange(exportedLabel.RequiredNodes);
				createLabel.IncludedNodes.AddRange(exportedLabel.IncludedNodes);
				updateGraph.Labels.Add(createLabel);
			}

			Dictionary<string, ExportedNode> nameToNode = graph.Groups.SelectMany(x => x.Nodes).ToDictionary(x => x.Name, x => x);
			foreach (ExportedBadge exportedBadge in graph.Badges)
			{
				RpcCreateLabelRequest createLabel = new RpcCreateLabelRequest();
				createLabel.UgsName = exportedBadge.Name;

				string? project = exportedBadge.Project;
				if (project != null && project.StartsWith("//", StringComparison.Ordinal))
				{
					int nextIdx = project.IndexOf('/', 2);
					if (nextIdx != -1)
					{
						nextIdx = project.IndexOf('/', nextIdx + 1);
						if (nextIdx != -1 && !project.Substring(nextIdx).Equals("/...", StringComparison.Ordinal))
						{
							createLabel.UgsProject = project.Substring(nextIdx + 1);
						}
					}
				}

				if (exportedBadge.Change == Batch.Change || exportedBadge.Change == 0)
				{
					createLabel.Change = RpcLabelChange.Current;
				}
				else if (exportedBadge.Change == Batch.CodeChange)
				{
					createLabel.Change = RpcLabelChange.Code;
				}
				else
				{
					logger.LogWarning("Badge is set to display for changelist {Change}. This is neither the current changelist ({CurrentChange}) or the current code changelist ({CurrentCodeChange}).", exportedBadge.Change, Batch.Change, Batch.CodeChange);
				}

				if (exportedBadge.Dependencies != null)
				{
					createLabel.RequiredNodes.AddRange(exportedBadge.Dependencies.Split(new char[] { ';' }, StringSplitOptions.RemoveEmptyEntries));

					HashSet<string> dependencies = new HashSet<string>();
					foreach (string requiredNode in createLabel.RequiredNodes)
					{
						GetRecursiveDependencies(requiredNode, nameToNode, dependencies);
					}
					createLabel.IncludedNodes.AddRange(dependencies);
				}
				updateGraph.Labels.Add(createLabel);
			}

			foreach (ExportedArtifact exportedArtifact in graph.Artifacts)
			{
				RpcCreateGraphArtifactRequest createArtifact = new RpcCreateGraphArtifactRequest();

				createArtifact.Name = exportedArtifact.Name;
				createArtifact.Type = exportedArtifact.Type ?? String.Empty;
				createArtifact.Description = exportedArtifact.Description ?? String.Empty;
				createArtifact.BasePath = exportedArtifact.BasePath ?? String.Empty;
				createArtifact.Keys.AddRange(exportedArtifact.Keys);
				createArtifact.Metadata.AddRange(exportedArtifact.Metadata);
				createArtifact.NodeName = exportedArtifact.NodeName;
				createArtifact.OutputName = exportedArtifact.OutputName;

				updateGraph.Artifacts.Add(createArtifact);
			}

			return updateGraph;
		}

		private static void GetRecursiveDependencies(string name, Dictionary<string, ExportedNode> nameToNode, HashSet<string> dependencies)
		{
			ExportedNode? node;
			if (nameToNode.TryGetValue(name, out node) && dependencies.Add(node.Name))
			{
				foreach (string inputDependency in node.InputDependencies)
				{
					GetRecursiveDependencies(inputDependency, nameToNode, dependencies);
				}
			}
		}

		protected async Task<bool> ExecuteAsync(JobStepInfo step, DirectoryReference workspaceDir, bool? useP4, ILogger logger, CancellationToken cancellationToken)
		{
			StringBuilder arguments = new StringBuilder("BuildGraph");
			if (_preprocessScript)
			{
				FileReference localPreprocessedScript = FileReference.Combine(workspaceDir, PreprocessedScript);
				arguments.AppendArgument(ScriptArgumentPrefix, localPreprocessedScript.FullName);

				FileReference localPreprocessedSchema = FileReference.Combine(workspaceDir, PreprocessedSchema);
				arguments.AppendArgument("-ImportSchema=", localPreprocessedSchema.FullName);

				ArtifactName artifactName = TempStorage.GetArtifactNameForNode(SetupStepName);

				RpcGetJobArtifactRequest artifactRequest = new RpcGetJobArtifactRequest();
				artifactRequest.JobId = JobId.ToString();
				artifactRequest.StepId = step.StepId.ToString();
				artifactRequest.Name = artifactName.ToString();
				artifactRequest.Type = ArtifactType.StepOutput.ToString();

				RpcGetJobArtifactResponse artifact = await JobRpc.GetArtifactAsync(artifactRequest, cancellationToken: cancellationToken);

				NamespaceId namespaceId = new NamespaceId(artifact.NamespaceId);
				RefName refName = new RefName(artifact.RefName);

				logger.LogInformation("Reading preprocessed script from {NamespaceId}:{RefName}", namespaceId, refName);

				IStorageNamespace storage = GetStorageNamespace(namespaceId, artifact.Token);

				DirectoryNode node = await storage.ReadRefTargetAsync<DirectoryNode>(refName, cancellationToken: cancellationToken);
				DirectoryNode? buildGraphDir = await node.TryOpenDirectoryAsync(BuildGraphTempStorageDir, cancellationToken: cancellationToken);
				if (buildGraphDir != null)
				{
					await buildGraphDir.ExtractAsync(new DirectoryInfo(workspaceDir.FullName), logger, cancellationToken);
					logger.LogInformation("Copying preprocessed script from {BuildGraphFolderName} into {OutputDir}", BuildGraphTempStorageDir, workspaceDir);
				}
				else
				{
					logger.LogInformation("Bundle has no {BuildGraphFolderName} folder; not copying any files", BuildGraphTempStorageDir);
				}
			}
			else if (_scriptFileName != null)
			{
				arguments.AppendArgument(ScriptArgumentPrefix, _scriptFileName);
			}

			if (!_allowTargetChanges)
			{
				foreach (string target in _targets)
				{
					arguments.AppendArgument(TargetArgumentPrefix + target);
				}
			}

			arguments.AppendArgument("-SingleNode=", step.Name);
			//			Arguments.AppendArgument("-TokenSignature=", JobId.ToString());

			foreach (string additionalArgument in _additionalArguments)
			{
				if (!_preprocessScript || !additionalArgument.StartsWith("-set:", StringComparison.OrdinalIgnoreCase))
				{
					arguments.AppendArgument(additionalArgument);
				}
			}

			bool result = await ExecuteWithTempStorageAsync(step, workspaceDir, arguments.ToString(), useP4, logger, cancellationToken);
			return result;
		}
		
		protected async Task CreateArtifactsAsync(JobStepId stepId, ArtifactName name, ArtifactType type, DirectoryReference baseDir, IEnumerable<(string, FileReference)> files, ILogger logger, CancellationToken cancellationToken)
		{
			await CreateArtifactAsync(stepId, name, type, baseDir, files.Select(x => x.Item2), logger, cancellationToken);
		}

		protected async Task CreateArtifactAsync(JobStepId stepId, ArtifactName name, ArtifactType type, DirectoryReference baseDir, IEnumerable<FileReference> files, ILogger logger, CancellationToken cancellationToken)
		{
			try
			{
				RpcCreateJobArtifactRequestV2 artifactRequest = new RpcCreateJobArtifactRequestV2();
				artifactRequest.JobId = JobId.ToString();
				artifactRequest.StepId = stepId.ToString();
				artifactRequest.Name = name.ToString();
				artifactRequest.Type = type.ToString();

				RpcCreateJobArtifactResponseV2 artifact = await JobRpc.CreateArtifactV2Async(artifactRequest, cancellationToken: cancellationToken);
				ArtifactId artifactId = ArtifactId.Parse(artifact.Id);
				Logger.LogInformation("Created artifact {ArtifactId} '{ArtifactName}' ({ArtifactType}) with ref {RefName} ({Link})", artifactId, name, type, artifact.RefName, $"{HordeClient.ServerUrl}/api/v1/storage/{artifact.NamespaceId}/refs/{artifact.RefName}");

				IStorageNamespace storage = GetStorageNamespace(new NamespaceId(artifact.NamespaceId), artifact.Token);

				IHashedBlobRef<DirectoryNode> rootRef;
				await using (IBlobWriter blobWriter = storage.CreateBlobWriter(new RefName(artifact.RefName)))
				{
					try
					{
						DirectoryNode dir = new DirectoryNode();
						await dir.AddFilesAsync(baseDir, files, blobWriter, progress: new UpdateStatsLogger(logger), cancellationToken: cancellationToken);
						rootRef = await blobWriter.WriteBlobAsync(dir, cancellationToken: cancellationToken);
					}
					catch (Exception ex)
					{
						Logger.LogInformation(ex, "Error uploading files for artifact {ArtifactId}", artifactId);
						throw;
					}
				}
				await storage.WriteRefAsync(new RefName(artifact.RefName), rootRef, cancellationToken: cancellationToken);

				Logger.LogInformation("Uploaded artifact {ArtifactId}", artifactId);
			}
			catch (Exception ex)
			{
				Logger.LogInformation(ex, "Error creating artifact '{Type}'", type);
			}
		}

		private async Task<bool> ExecuteWithTempStorageAsync(JobStepInfo step, DirectoryReference workspaceDir, string arguments, bool? useP4, ILogger logger, CancellationToken cancellationToken)
		{
			DirectoryReference manifestDir = DirectoryReference.Combine(workspaceDir, $"{EnginePath}/Saved/BuildGraph");

			// Create the mapping of tag names to file sets
			Dictionary<string, HashSet<FileReference>> tagNameToFileSet = new Dictionary<string, HashSet<FileReference>>();

			// Read all the input tags for this node, and build a list of referenced input storage blocks
			HashSet<TempStorageBlockRef> inputStorageBlocks = new HashSet<TempStorageBlockRef>();
			foreach (string input in step.Inputs)
			{
				int slashIdx = input.IndexOf('/', StringComparison.Ordinal);
				if (slashIdx == -1)
				{
					logger.LogError("Missing slash from node input: {Input}", input);
					return false;
				}

				string nodeName = input.Substring(0, slashIdx);
				string tagName = input.Substring(slashIdx + 1);

				TempStorageTagManifest fileList = await TempStorage.RetrieveTagAsync(HordeClient, JobId, step.StepId, nodeName, tagName, manifestDir, logger, cancellationToken);
				tagNameToFileSet[tagName] = fileList.ToFileSet(workspaceDir);
				inputStorageBlocks.UnionWith(fileList.Blocks);
			}

			// Read the manifests for all the input storage blocks
			Dictionary<TempStorageBlockRef, TempStorageBlockManifest> inputManifests = new Dictionary<TempStorageBlockRef, TempStorageBlockManifest>();

			{
				using TelemetrySpan span = Tracer.StartActiveSpan("TempStorage");
				const string Prefix = "horde.temp_storage.";
				span.SetAttribute(Prefix + "action", "download");
				
				Stopwatch timer = Stopwatch.StartNew();
				span.SetAttribute(Prefix + "blocks", inputStorageBlocks.Count);
				foreach (TempStorageBlockRef inputStorageBlock in inputStorageBlocks)
				{
					TempStorageBlockManifest manifest = await TempStorage.RetrieveBlockAsync(HordeClient, JobId, step.StepId, inputStorageBlock.NodeName, inputStorageBlock.OutputName, workspaceDir, manifestDir, logger, cancellationToken);
					inputManifests[inputStorageBlock] = manifest;
				}
				span.SetAttribute(Prefix + "size", inputManifests.Sum(x => x.Value.GetTotalSize()));
				logger.LogInformation("Download took {Time:n1}s", timer.Elapsed.TotalSeconds);
			}

			// Read all the input storage blocks, keeping track of which block each file came from
			Dictionary<string, (TempStorageFile, TempStorageBlockRef)> inputPathToSource = new Dictionary<string, (TempStorageFile, TempStorageBlockRef)>(FileReference.Comparer);
			foreach ((TempStorageBlockRef inputStorageBlock, TempStorageBlockManifest inputManifest) in inputManifests)
			{
				foreach (TempStorageFile inputFile in inputManifest.Files)
				{
					(TempStorageFile File, TempStorageBlockRef BlockRef) source;
					if (!inputPathToSource.TryGetValue(inputFile.RelativePath, out source))
					{
						inputPathToSource.Add(inputFile.RelativePath, (inputFile, inputStorageBlock));
					}
					else if (!inputFile.Equals(source.File) && !TempStorage.IsDuplicateBuildProduct(inputFile.ToFileReference(workspaceDir)))
					{
						logger.LogInformation("File '{File}' was produced by {InputBlock} and {CurrentBlock}", inputFile.RelativePath, inputStorageBlock.ToString(), source.BlockRef.ToString());
					}
				}
			}

			// Run UAT
			if (await ExecuteAutomationToolAsync(step, workspaceDir, arguments, useP4, logger, cancellationToken) != 0)
			{
				return false;
			}

			// Read all the output manifests
			foreach (string tagName in step.OutputNames)
			{
				FileReference tagFileListLocation = TempStorage.GetTagManifestLocation(manifestDir, step.Name, tagName);
				logger.LogInformation("Reading local file list from {File}", tagFileListLocation.FullName);

				TempStorageTagManifest fileList = TempStorageTagManifest.Load(tagFileListLocation);
				tagNameToFileSet[tagName] = fileList.ToFileSet(workspaceDir);
			}

			// Check that none of the inputs have been clobbered
			Dictionary<string, string> modifiedFiles = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
			foreach (TempStorageFile file in inputManifests.Values.SelectMany(x => x.Files))
			{
				string? message;
				if (!modifiedFiles.ContainsKey(file.RelativePath) && !file.Compare(workspaceDir, out message))
				{
					modifiedFiles.Add(file.RelativePath, message);
				}
			}
			if (modifiedFiles.Count > 0)
			{
				const int NumFiles = 50;

				string modifiedFileList = "";
				if (modifiedFiles.Count < NumFiles)
				{
					modifiedFileList = String.Join("\n", modifiedFiles.Select(x => x.Value));
				}
				else
				{
					modifiedFileList = String.Join("\n", modifiedFiles.Take(NumFiles).Select(x => x.Value));
					modifiedFileList += $"{Environment.NewLine}...and {modifiedFiles.Count - NumFiles} more.";
				}

				logger.LogInformation("Build product(s) from a previous step have been modified:\n{FileList}", modifiedFileList);
			}

			// Determine all the output files which are required to be copied to temp storage (because they're referenced by nodes in another agent)
			HashSet<FileReference> referencedOutputFiles = new HashSet<FileReference>();
			foreach (int publishOutput in step.PublishOutputs)
			{
				string tagName = step.OutputNames[publishOutput];
				referencedOutputFiles.UnionWith(tagNameToFileSet[tagName]);
			}

			// Find a block name for all new outputs
			Dictionary<FileReference, string> outputFileToBlockName = new Dictionary<FileReference, string>();
			for (int idx = 0; idx < step.OutputNames.Count; idx++)
			{
				string tagName = step.OutputNames[idx];

				string outputNameWithoutHash = tagName.TrimStart('#');
				bool isDefaultOutput = outputNameWithoutHash.Equals(step.Name, StringComparison.OrdinalIgnoreCase);

				HashSet<FileReference> files = tagNameToFileSet[tagName];
				foreach (FileReference file in files)
				{
					if (file.IsUnderDirectory(workspaceDir))
					{
						if (isDefaultOutput)
						{
							if (!outputFileToBlockName.ContainsKey(file))
							{
								outputFileToBlockName[file] = "";
							}
						}
						else
						{
							string? blockName;
							if (outputFileToBlockName.TryGetValue(file, out blockName) && blockName.Length > 0)
							{
								outputFileToBlockName[file] = $"{blockName}+{outputNameWithoutHash}";
							}
							else
							{
								outputFileToBlockName[file] = outputNameWithoutHash;
							}
						}
					}
				}
			}

			// Invert the dictionary to make a mapping of storage block to the files each contains
			Dictionary<string, HashSet<FileReference>> outputStorageBlockToFiles = new Dictionary<string, HashSet<FileReference>>();
			foreach (KeyValuePair<FileReference, string> pair in outputFileToBlockName)
			{
				HashSet<FileReference>? files;
				if (!outputStorageBlockToFiles.TryGetValue(pair.Value, out files))
				{
					files = new HashSet<FileReference>();
					outputStorageBlockToFiles.Add(pair.Value, files);
				}
				files.Add(pair.Key);
			}

			// Write all the storage blocks, and update the mapping from file to storage block
			{
				using TelemetrySpan span = Tracer.StartActiveSpan("TempStorage");
				const string Prefix = "horde.temp_storage.";
				span.SetAttribute(Prefix + "action", "upload");
				
				// Create the artifact
				RpcCreateJobArtifactRequestV2 artifactRequest = new RpcCreateJobArtifactRequestV2();
				artifactRequest.JobId = JobId.ToString();
				artifactRequest.StepId = step.StepId.ToString();
				artifactRequest.Name = TempStorage.GetArtifactNameForNode(step.Name).ToString();
				artifactRequest.Type = ArtifactType.StepOutput.ToString();

				RpcCreateJobArtifactResponseV2 artifact = await JobRpc.CreateArtifactV2Async(artifactRequest, cancellationToken: cancellationToken);
				ArtifactId artifactId = ArtifactId.Parse(artifact.Id);
				logger.LogInformation("Created artifact {ArtifactId} '{ArtifactName}' ({ArtifactType}) with ref {RefName} ({RefUrl})", artifactId, artifactRequest.Name, ArtifactType.StepOutput, artifact.RefName, $"{HordeClient.ServerUrl.ToString().TrimEnd('/')}/api/v1/storage/{artifact.NamespaceId}/refs/{artifact.RefName}");
				DecorateSpanWithArtifact(span, Prefix, artifactRequest, artifact);

				IStorageNamespace storage = GetStorageNamespace(new NamespaceId(artifact.NamespaceId), artifact.Token);

				// Upload the data
				Stopwatch timer = Stopwatch.StartNew();

				IHashedBlobRef<DirectoryNode> outputNodeRef;
				await using (IBlobWriter blobWriter = storage.CreateBlobWriter(artifact.RefName))
				{
					DirectoryNode outputNode = new DirectoryNode();

					// Create all the output blocks
					Dictionary<FileReference, TempStorageBlockRef> outputFileToStorageBlock = new Dictionary<FileReference, TempStorageBlockRef>();
					foreach (KeyValuePair<string, HashSet<FileReference>> pair in outputStorageBlockToFiles)
					{
						TempStorageBlockRef outputBlock = new TempStorageBlockRef(step.Name, pair.Key);
						foreach (FileReference file in pair.Value)
						{
							outputFileToStorageBlock.Add(file, outputBlock);
						}
						if (pair.Value.Any(x => referencedOutputFiles.Contains(x)))
						{
							outputNode.AddDirectory(await TempStorage.ArchiveBlockAsync(manifestDir, step.Name, pair.Key, workspaceDir, pair.Value.ToArray(), blobWriter, logger, cancellationToken));
						}
					}

					// Create all the output tags
					foreach (string outputName in step.OutputNames)
					{
						HashSet<FileReference> files = tagNameToFileSet[outputName];

						HashSet<TempStorageBlockRef> storageBlocks = new HashSet<TempStorageBlockRef>();
						foreach (FileReference file in files)
						{
							TempStorageBlockRef? storageBlock;
							if (outputFileToStorageBlock.TryGetValue(file, out storageBlock))
							{
								storageBlocks.Add(storageBlock);
							}
						}

						outputNode.AddFile(await TempStorage.ArchiveTagAsync(manifestDir, step.Name, outputName, workspaceDir, files, storageBlocks.ToArray(), blobWriter, logger, cancellationToken));
					}

					outputNodeRef = await blobWriter.WriteBlobAsync(outputNode, cancellationToken: cancellationToken);
				}

				// Write the final node
				await storage.WriteRefAsync(artifact.RefName, outputNodeRef, new RefOptions(), cancellationToken: cancellationToken);
				logger.LogInformation("Upload took {Time:n1}s", timer.Elapsed.TotalSeconds);
			}

			// Create all the named artifacts. TODO: Merge this with regular temp storage artifacts?
			logger.LogInformation("Uploading {NumArtifacts} artifacts", step.Artifacts.Count);
			foreach (RpcCreateGraphArtifactRequest graphArtifact in step.Artifacts)
			{
				logger.LogInformation("Uploading artifact {Name} using output {Output}", graphArtifact.Name, graphArtifact.OutputName);

				HashSet<FileReference>? files;
				if (!tagNameToFileSet.TryGetValue(graphArtifact.OutputName, out files))
				{
					logger.LogWarning("Missing output fileset for artifact {Name} (output={OutputName})", graphArtifact.Name, graphArtifact.OutputName);
					continue;
				}

				{
					using TelemetrySpan span = Tracer.StartActiveSpan("Artifact");
					const string Prefix = "horde.artifact.";
					span.SetAttribute(Prefix + "action", "upload");
					span.SetAttribute(Prefix + "name", graphArtifact.Name);
					span.SetAttribute(Prefix + "type", graphArtifact.Type);
					
					// Create the artifact
					RpcCreateJobArtifactRequestV2 artifactRequest = new RpcCreateJobArtifactRequestV2();
					artifactRequest.JobId = JobId.ToString();
					artifactRequest.StepId = step.StepId.ToString();
					artifactRequest.Name = graphArtifact.Name;
					artifactRequest.Type = graphArtifact.Type;
					artifactRequest.Description = graphArtifact.Description;
					artifactRequest.Keys.AddRange(graphArtifact.Keys);
					artifactRequest.Metadata.AddRange(graphArtifact.Metadata);

					RpcCreateJobArtifactResponseV2 artifact = await JobRpc.CreateArtifactV2Async(artifactRequest, cancellationToken: cancellationToken);
					ArtifactId artifactId = ArtifactId.Parse(artifact.Id);
					logger.LogInformation("Created artifact {ArtifactId} '{ArtifactName}' ({ArtifactType}) with ref {RefName} ({RefUrl})", artifactId, artifactRequest.Name, ArtifactType.StepOutput, artifact.RefName, $"{HordeClient.ServerUrl.ToString().TrimEnd('/')}/api/v1/storage/{artifact.NamespaceId}/refs/{artifact.RefName}");
					DecorateSpanWithArtifact(span, Prefix, artifactRequest, artifact);

					IStorageNamespace storage = GetStorageNamespace(new NamespaceId(artifact.NamespaceId), artifact.Token);

					// Upload the data
					Stopwatch timer = Stopwatch.StartNew();

					IHashedBlobRef<DirectoryNode> outputNodeRef;
					await using (IBlobWriter blobWriter = storage.CreateBlobWriter(artifact.RefName))
					{
						DirectoryNode outputNode = new DirectoryNode();

						DirectoryReference baseDir = DirectoryReference.Combine(workspaceDir, graphArtifact.BasePath);
						if (DirectoryReference.Exists(baseDir))
						{
							await outputNode.AddFilesAsync(baseDir, files, blobWriter, cancellationToken: cancellationToken);
						}
						else
						{
							logger.LogWarning("Base path for artifact {ArtifactName} does not exist ({Path})", graphArtifact.Name, baseDir);
						}

						outputNodeRef = await blobWriter.WriteBlobAsync(outputNode, cancellationToken: cancellationToken);
					}

					// Write the final node
					await storage.WriteRefAsync(artifact.RefName, outputNodeRef, new RefOptions(), cancellationToken: cancellationToken);
					logger.LogInformation("Upload took {Time:n1}s", timer.Elapsed.TotalSeconds);
				}
			}

			return true;
		}

		protected async Task<int> ExecuteAutomationToolAsync(JobStepInfo step, DirectoryReference workspaceDir, string? arguments, bool? useP4, ILogger logger, CancellationToken cancellationToken)
		{
			int result;
			using TelemetrySpan span = Tracer.StartActiveSpan("BuildGraph");
			span.SetAttribute("horde.job.step.id", step.StepId.ToString());
			span.SetAttribute("horde.job.step.name", step.Name);

			if (!_compileAutomationTool)
			{
				arguments += " -NoCompile";
			}

			if (useP4 is false)
			{
				arguments += " -NoP4";
			}
			
			span.SetAttribute("horde.args", arguments);

			if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
			{
				result = await ExecuteCommandAsync(step, workspaceDir, Environment.GetEnvironmentVariable("COMSPEC") ?? "cmd.exe", $"/C \"\"{workspaceDir}\\{EnginePath}\\Build\\BatchFiles\\RunUAT.bat\" {arguments}\"", span, logger, cancellationToken);
			}
			else if (RuntimeInformation.IsOSPlatform(OSPlatform.Linux))
			{
				string args = $"\"{workspaceDir}/{EnginePath}/Build/BatchFiles/RunUAT.sh\" {arguments}";

				if (JobOptions.UseWine is true)
				{
					args = $"\"{workspaceDir}/{EnginePath}/Build/BatchFiles/RunWineUAT.sh\" {arguments}";
				}

				result = await ExecuteCommandAsync(step, workspaceDir, "/bin/bash", args, span, logger, cancellationToken);
			}
			else if (RuntimeInformation.IsOSPlatform(OSPlatform.OSX))
			{
				result = await ExecuteCommandAsync(step, workspaceDir, "/bin/sh", $"\"{workspaceDir}/{EnginePath}/Build/BatchFiles/RunUAT.sh\" {arguments}", span, logger, cancellationToken);
			}
			else
			{
				throw new Exception("Unsupported platform");
			}

			_compileAutomationTool = false;
			return result;
		}

		static void AddRestrictedDirs(List<DirectoryReference> directories, string subFolder)
		{
			int numDirs = directories.Count;
			for (int idx = 0; idx < numDirs; idx++)
			{
				DirectoryReference subDir = DirectoryReference.Combine(directories[idx], subFolder);
				if (DirectoryReference.Exists(subDir))
				{
					directories.AddRange(DirectoryReference.EnumerateDirectories(subDir));
				}
			}
		}

		async Task<List<string>> ReadIgnorePatternsAsync(DirectoryReference workspaceDir, ILogger logger)
		{
			List<DirectoryReference> baseDirs = new List<DirectoryReference>();
			baseDirs.Add(DirectoryReference.Combine(workspaceDir, EnginePath));
			AddRestrictedDirs(baseDirs, "Restricted");
			AddRestrictedDirs(baseDirs, "Platforms");

			List<string> ignorePatternLines = new List<string>();
			foreach (DirectoryReference baseDir in baseDirs)
			{
				FileReference ignorePatternFile = FileReference.Combine(baseDir, "Build", "Horde", "IgnorePatterns.txt");
				if (FileReference.Exists(ignorePatternFile))
				{
					logger.LogInformation("Reading ignore patterns from {File}...", ignorePatternFile);
					ignorePatternLines.AddRange(await FileReference.ReadAllLinesAsync(ignorePatternFile));
				}
			}

			HashSet<string> ignorePatterns = new HashSet<string>(StringComparer.Ordinal);
			foreach (string line in ignorePatternLines)
			{
				string trimLine = line.Trim();
				if (trimLine.Length > 0 && trimLine[0] != '#')
				{
					ignorePatterns.Add(trimLine);
				}
			}

			return ignorePatterns.ToList();
		}

		static FileReference GetCleanupScript(DirectoryReference workspaceDir)
		{
			if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
			{
				return FileReference.Combine(workspaceDir, "Cleanup.bat");
			}
			else
			{
				return FileReference.Combine(workspaceDir, "Cleanup.sh");
			}
		}

		static FileReference GetLeaseCleanupScript(DirectoryReference workspaceDir)
		{
			if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
			{
				return FileReference.Combine(workspaceDir, "CleanupLease.bat");
			}
			else
			{
				return FileReference.Combine(workspaceDir, "CleanupLease.sh");
			}
		}

		internal IReadOnlyDictionary<string, string> GetEnvVars()
		{
			return new Dictionary<string, string>(_envVars);
		}

		protected static async Task ExecuteLeaseCleanupScriptAsync(DirectoryReference workspaceDir, ILogger logger)
		{
			using (LogParser parser = new LogParser(logger, new List<string>()))
			{
				FileReference leaseCleanupScript = GetLeaseCleanupScript(workspaceDir);
				await ExecuteCleanupScriptAsync(leaseCleanupScript, parser, logger);
			}
		}

		protected async Task TerminateProcessesAsync(TerminateCondition condition, ILogger logger)
		{
			try
			{
				await TerminateProcessHelper.TerminateProcessesAsync(condition, WorkingDir, ProcessesToTerminate, logger, CancellationToken.None);
			}
			catch (Exception ex)
			{
				logger.LogWarning(ex, "Exception while terminating processes: {Message}", ex.Message);
			}
		}

		static async Task ExecuteCleanupScriptAsync(FileReference cleanupScript, LogParser filter, ILogger logger)
		{
			if (FileReference.Exists(cleanupScript))
			{
				filter.WriteLine($"Executing cleanup script: {cleanupScript}");

				string fileName;
				string arguments;
				if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
				{
					fileName = "C:\\Windows\\System32\\Cmd.exe";
					arguments = $"/C \"{cleanupScript}\"";
				}
				else
				{
					fileName = "/bin/sh";
					arguments = $"\"{cleanupScript}\"";
				}

				try
				{
					using (CancellationTokenSource cancellationSource = new CancellationTokenSource())
					{
						cancellationSource.CancelAfter(TimeSpan.FromSeconds(30.0));
						await ExecuteProcessAsync(fileName, arguments, null, filter, logger, cancellationSource.Token);
					}
					FileUtils.ForceDeleteFile(cleanupScript);
				}
				catch (OperationCanceledException)
				{
					filter.WriteLine("Cleanup script did not complete within allotted time. Aborting.");
				}
			}
		}

		static async Task<int> ExecuteProcessAsync(string fileName, string arguments, IReadOnlyDictionary<string, string>? newEnvironment, LogParser filter, ILogger logger, CancellationToken cancellationToken)
		{
			logger.LogInformation("Executing {File} {Arguments}", fileName.QuoteArgument(), arguments);

			using (CancellationTokenSource cancellationSource = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken))
			{
				using (ManagedProcessGroup processGroup = new ManagedProcessGroup())
				{
					using (ManagedProcess process = new ManagedProcess(processGroup, fileName, arguments, null, newEnvironment, null, ProcessPriorityClass.Normal))
					{
						bool hasExited = false;
						try
						{
							async Task WaitForExitOrCancelAsync()
							{
								await process.WaitForExitAsync(cancellationToken);
								hasExited = true;
								cancellationSource.CancelAfter(TimeSpan.FromSeconds(60.0));
							}

							Task cancelTask = WaitForExitOrCancelAsync();
							await process.CopyToAsync((buffer, offset, length) => filter.WriteData(buffer.AsMemory(offset, length)), 4096, cancellationSource.Token);
							await cancelTask;
						}
						catch (OperationCanceledException) when (hasExited)
						{
							logger.LogWarning("Process exited without closing output pipes; they may have been inherited by a child process that is still running.");
						}
						return process.ExitCode;
					}
				}
			}
		}

		/// <summary>
		/// Execute a process inside a Linux container
		/// </summary>
		/// <param name="arguments">Arguments</param>
		/// <param name="mountDirs">Directories to mount inside the container for read/write</param>
		/// <param name="newEnvironment">Environment variables</param>
		/// <param name="filter">Log parser</param>
		/// <param name="logger">Logger</param>
		/// <param name="cancellationToken">Cancellation token for the call</param>
		/// <returns></returns>
		/// <exception cref="Exception"></exception>
		async Task<int> ExecuteProcessInContainerAsync(string arguments, List<DirectoryReference> mountDirs, IReadOnlyDictionary<string, string>? newEnvironment, LogParser filter, ILogger logger, CancellationToken cancellationToken)
		{
			if (!RuntimeInformation.IsOSPlatform(OSPlatform.Linux))
			{
				throw new Exception("Only Linux is supported for executing a process inside a container");
			}

			if (String.IsNullOrEmpty(JobOptions.Container.ImageUrl))
			{
				throw new Exception("Image URL is null or empty");
			}

			// Default to "docker" executable and assume it's available on the path
			string executable = JobOptions.Container.ContainerEngineExecutable ?? "docker";

			uint linuxUid = LinuxInterop.getuid();
			uint linuxGid = LinuxInterop.getgid();

			List<string> containerArgs = new()
				{
					"run",
					"--tty", // Allocate a pseudo-TTY
					$"--name horde-job-{JobId}-{BatchId}", // Better name for debugging purposes
					"--rm", // Ensure container is removed after run
					$"--user {linuxUid}:{linuxGid}" // Run container as current user (important for mounted dirs)
				};

			foreach (DirectoryReference mountDir in mountDirs)
			{
				containerArgs.Add($"--volume {mountDir.FullName}:{mountDir.FullName}:rw");
			}

			if (newEnvironment != null)
			{
				string envFilePath = Path.GetTempFileName();
				StringBuilder sb = new();
				foreach ((string key, string value) in newEnvironment)
				{
					sb.AppendLine($"{key}={value}");
				}
				await File.WriteAllTextAsync(envFilePath, sb.ToString(), cancellationToken);
				containerArgs.Add("--env-file=" + envFilePath);
			}

			if (!String.IsNullOrEmpty(JobOptions.Container.ExtraArguments))
			{
				containerArgs.Add(JobOptions.Container.ExtraArguments);
			}

			containerArgs.Add(JobOptions.Container.ImageUrl);
			string containerArgStr = String.Join(' ', containerArgs);
			arguments = containerArgStr + " " + arguments;

			logger.LogInformation("Executing {File} {Arguments} in container", executable.QuoteArgument(), arguments);

			// Skip forwarding of env vars as they are explicitly set above as arguments to container run
			return await ExecuteProcessAsync(executable, arguments, new Dictionary<string, string>(), filter, logger, cancellationToken);
		}

		private static List<DirectoryReference> GetContainerMountDirs(DirectoryReference workspaceDir, IReadOnlyDictionary<string, string> envVars)
		{
			List<DirectoryReference> dirs = new();
			dirs.Add(workspaceDir);
			if (envVars.TryGetValue("UE_SDKS_ROOT", out string? autoSdkDirPath))
			{
				dirs.Add(new DirectoryReference(autoSdkDirPath));
			}
			return dirs;
		}

		private static string ConformEnvironmentVariableName(string name)
		{
			if (!RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
			{
				// Non-windows platforms don't allow dashes in variable names. The engine platform layer substitutes underscores for them.
				return name.Replace('-', '_');
			}
			return name;
		}

		async Task<int> ExecuteCommandAsync(JobStepInfo step, DirectoryReference workspaceDir, string fileName, string arguments, TelemetrySpan buildGraphSpan, ILogger jobLogger, CancellationToken cancellationToken)
		{
			// Method for expanding environment variable properties related to this step
			Dictionary<string, string> properties = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
			properties.Add("RootDir", workspaceDir.FullName);

			// Combine all the supplied environment variables together
			Dictionary<string, string> newEnvVars = new Dictionary<string, string>(StringComparer.Ordinal);
			foreach (KeyValuePair<string, string> envVar in _envVars)
			{
				newEnvVars[ConformEnvironmentVariableName(envVar.Key)] = StringUtils.ExpandProperties(envVar.Value, properties);
			}
			foreach (KeyValuePair<string, string> envVar in step.EnvVars)
			{
				newEnvVars[ConformEnvironmentVariableName(envVar.Key)] = StringUtils.ExpandProperties(envVar.Value, properties);
			}
			foreach (KeyValuePair<string, string> envVar in step.Credentials)
			{
				newEnvVars[ConformEnvironmentVariableName(envVar.Key)] = envVar.Value;
			}

			// Add all the other Horde-specific variables
			newEnvVars["IsBuildMachine"] = "1";

			DirectoryReference logDir = DirectoryReference.Combine(workspaceDir, $"{EnginePath}/Programs/AutomationTool/Saved/Logs");
			FileUtils.ForceDeleteDirectoryContents(logDir);
			newEnvVars["uebp_LogFolder"] = logDir.FullName;

			DirectoryReference telemetryDir = DirectoryReference.Combine(workspaceDir, $"{EnginePath}/Programs/AutomationTool/Saved/Telemetry");
			FileUtils.ForceDeleteDirectoryContents(telemetryDir);
			newEnvVars["UE_TELEMETRY_DIR"] = telemetryDir.FullName;

			DirectoryReference testDataDir = DirectoryReference.Combine(workspaceDir, $"{EnginePath}/Programs/AutomationTool/Saved/TestData");
			FileUtils.ForceDeleteDirectoryContents(testDataDir);
			newEnvVars["UE_TESTDATA_DIR"] = testDataDir.FullName;

			FileReference graphUpdateFile = FileReference.Combine(workspaceDir, $"{EnginePath}/Saved/Horde/Graph.json");
			FileUtils.ForceDeleteFile(graphUpdateFile);
			newEnvVars["UE_HORDE_GRAPH_UPDATE"] = graphUpdateFile.FullName;

			try
			{
				// TODO: These are AWS specific, this should be extended to handle more clouds or for licensees to be able to set these
				newEnvVars["UE_HORDE_AVAILABILITY_ZONE"] = Amazon.Util.EC2InstanceMetadata.AvailabilityZone ?? "";
				newEnvVars["UE_HORDE_REGION"] = Amazon.Util.EC2InstanceMetadata.Region?.DisplayName ?? "";
			}
			catch
			{
			}

			newEnvVars["UE_HORDE_JOBID"] = JobId.ToString();
			newEnvVars["UE_HORDE_BATCHID"] = BatchId.ToString();
			newEnvVars["UE_HORDE_STEPID"] = step.StepId.ToString();
			newEnvVars["UE_HORDE_STREAMID"] = Batch.StreamId;

			// Enable structured logging output
			newEnvVars["UE_LOG_JSON_TO_STDOUT"] = "1";

			// Pass the location of the cleanup script to the job
			FileReference cleanupScript = GetCleanupScript(workspaceDir);
			newEnvVars["UE_HORDE_CLEANUP"] = cleanupScript.FullName;

			// Pass the location of the cleanup script to the job
			FileReference leaseCleanupScript = GetLeaseCleanupScript(workspaceDir);
			newEnvVars["UE_HORDE_LEASE_CLEANUP"] = leaseCleanupScript.FullName;

			// Set up the shared working dir
			newEnvVars["UE_HORDE_SHARED_DIR"] = DirectoryReference.Combine(WorkingDir, "Saved").FullName;

			// Disable the S3DDC. This is technically a Fortnite-specific setting, but affects a large number of branches and is hard to retrofit. 
			// Setting here for now, since it's likely to be temporary.
			if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
			{
				newEnvVars["UE-S3DataCachePath"] = "None";
			}

			// Log all the environment variables to the log
			HashSet<string> credentialKeys = new HashSet<string>(step.Credentials.Keys, StringComparer.OrdinalIgnoreCase);
			credentialKeys.Add(HordeHttpClient.HordeTokenEnvVarName);

			foreach (KeyValuePair<string, string> envVar in newEnvVars.OrderBy(x => x.Key))
			{
				string value = "[redacted]";
				if (!credentialKeys.Contains(envVar.Key))
				{
					value = envVar.Value;
				}
				jobLogger.LogInformation("Setting env var: {Key}={Value}", envVar.Key, value);
			}

			// Add all the old environment variables into the list
			foreach (object? envVar in Environment.GetEnvironmentVariables())
			{
				System.Collections.DictionaryEntry entry = (System.Collections.DictionaryEntry)envVar!;
				string key = entry.Key.ToString()!;
				if (!newEnvVars.ContainsKey(key))
				{
					newEnvVars[key] = entry.Value!.ToString()!;
				}
			}

			// Clear out the telemetry directory
			if (DirectoryReference.Exists(telemetryDir))
			{
				FileUtils.ForceDeleteDirectoryContents(telemetryDir);
			}

			List<string> ignorePatterns = await ReadIgnorePatternsAsync(workspaceDir, jobLogger);

			int exitCode;
			using (LogParser filter = new LogParser(jobLogger, ignorePatterns))
			{
				await ExecuteCleanupScriptAsync(cleanupScript, filter, jobLogger);
				try
				{
					if (RuntimeInformation.IsOSPlatform(OSPlatform.Linux) && JobOptions?.Container?.Enabled is true)
					{
						List<DirectoryReference> mountDirs = GetContainerMountDirs(workspaceDir, newEnvVars);
						exitCode = await ExecuteProcessInContainerAsync(arguments, mountDirs, newEnvVars, filter, jobLogger, cancellationToken);
					}
					else
					{
						exitCode = await ExecuteProcessAsync(fileName, arguments, newEnvVars, filter, jobLogger, cancellationToken);
					}
				}
				finally
				{
					await ExecuteCleanupScriptAsync(cleanupScript, filter, jobLogger);
				}
				filter.Flush();
			}

			if (DirectoryReference.Exists(telemetryDir))
			{
				List<(string, FileReference)> telemetryFiles = new List<(string, FileReference)>();
				try
				{
					await CreateTelemetryFilesAsync(step.Name, telemetryDir, telemetryFiles, buildGraphSpan, jobLogger, cancellationToken);
				}
				catch (Exception ex)
				{
					jobLogger.LogInformation(ex, "Unable to parse/upload telemetry files: {Message}", ex.Message);
				}

				await CreateArtifactsAsync(step.StepId, TempStorage.GetArtifactNameForNode(step.Name), ArtifactType.StepTrace, workspaceDir, telemetryFiles, jobLogger, cancellationToken);

				foreach ((_, FileReference telemetryFile) in telemetryFiles)
				{
					FileUtils.ForceDeleteFile(telemetryFile);
				}
			}

			if (DirectoryReference.Exists(testDataDir))
			{
				List<(string, FileReference)> testDataFiles = new List<(string, FileReference)>();

				Dictionary<string, object> combinedTestData = new Dictionary<string, object>();
				foreach (FileReference testDataFile in DirectoryReference.EnumerateFiles(testDataDir, "*.json", SearchOption.AllDirectories))
				{
					jobLogger.LogInformation("Reading test data {TestDataFile}", testDataFile);
					testDataFiles.Add(($"TestData/{testDataFile.MakeRelativeTo(testDataDir)}", testDataFile));

					TestData testData;
					using (FileStream stream = FileReference.Open(testDataFile, FileMode.Open))
					{
						JsonSerializerOptions options = new JsonSerializerOptions { PropertyNameCaseInsensitive = true };
						testData = (await JsonSerializer.DeserializeAsync<TestData>(stream, options, cancellationToken))!;
					}

					foreach (TestDataItem item in testData.Items)
					{
						if (combinedTestData.ContainsKey(item.Key))
						{
							jobLogger.LogWarning("Key '{Key}' already exists - ignoring", item.Key);
						}
						else
						{
							jobLogger.LogDebug("Adding data with key '{Key}'", item.Key);
							combinedTestData.Add(item.Key, item.Data);
						}
					}
				}

				jobLogger.LogInformation("Found {NumResults} test results", combinedTestData.Count);
				await UploadTestDataAsync(step.StepId, combinedTestData);

				await CreateArtifactsAsync(step.StepId, TempStorage.GetArtifactNameForNode(step.Name), ArtifactType.StepTestData, workspaceDir, testDataFiles, jobLogger, cancellationToken);
			}

			if (DirectoryReference.Exists(logDir))
			{
				List<FileReference> artifactFiles = DirectoryReference.EnumerateFiles(logDir, "*", SearchOption.AllDirectories).ToList();
				await CreateArtifactAsync(step.StepId, TempStorage.GetArtifactNameForNode(step.Name), ArtifactType.StepSaved, workspaceDir, artifactFiles, jobLogger, cancellationToken);

				foreach (FileReference reportFile in artifactFiles.Where(x => x.HasExtension(".report.json")))
				{
					try
					{
						await CreateReportAsync(step.StepId, reportFile, jobLogger);
					}
					catch (Exception ex)
					{
						jobLogger.LogWarning("Unable to upload report: {Message}", ex.Message);
					}
				}
			}

			if (FileReference.Exists(graphUpdateFile))
			{
				jobLogger.LogInformation("Parsing graph update from {File}", graphUpdateFile);

				RpcUpdateGraphRequest updateGraph = await ParseGraphUpdateAsync(graphUpdateFile, jobLogger, cancellationToken);
				foreach (RpcCreateGroupRequest group in updateGraph.Groups)
				{
					jobLogger.LogInformation("  AgentType: {Name}", group.AgentType);
					foreach (RpcCreateNodeRequest node in group.Nodes)
					{
						jobLogger.LogInformation("    Node: {Name}", node.Name);
					}
				}

				await JobRpc.UpdateGraphAsync(updateGraph, null, null, cancellationToken);

				HashSet<string> publishOutputNames = new HashSet<string>(updateGraph.Groups.SelectMany(x => x.Nodes).SelectMany(x => x.Inputs), StringComparer.OrdinalIgnoreCase);
				foreach (string publishOutputName in publishOutputNames)
				{
					jobLogger.LogInformation("Required output: {OutputName}", publishOutputName);
				}
				foreach (string stepOutputName in step.OutputNames)
				{
					jobLogger.LogInformation("Output from current step: {OutputName}", stepOutputName);
				}

				for (int idx = 0; idx < step.OutputNames.Count; idx++)
				{
					if (!step.PublishOutputs.Contains(idx) && publishOutputNames.Contains(step.OutputNames[idx]))
					{
						jobLogger.LogInformation("Added new publish output on {Name}", step.OutputNames[idx]);
						step.PublishOutputs.Add(idx);
					}
				}
			}

			return exitCode;
		}

		async Task CreateTelemetryFilesAsync(string stepName, DirectoryReference telemetryDir, List<(string, FileReference)> telemetryFiles, TelemetrySpan buildGraphSpan, ILogger jobLogger, CancellationToken cancellationToken)
		{
			List<TraceEventList> telemetryList = new List<TraceEventList>();
			foreach (FileReference telemetryFile in DirectoryReference.EnumerateFiles(telemetryDir, "*.json"))
			{
				jobLogger.LogInformation("Reading telemetry from {File}", telemetryFile);
				byte[] data = await FileReference.ReadAllBytesAsync(telemetryFile, cancellationToken);

				TraceEventList telemetry = JsonSerializer.Deserialize<TraceEventList>(data.AsSpan())!;
				if (telemetry.Spans.Count > 0)
				{
					string defaultServiceName = telemetryFile.GetFileNameWithoutAnyExtensions();
					foreach (TraceEvent span in telemetry.Spans)
					{
						span.Service ??= defaultServiceName;
					}
					telemetryList.Add(telemetry);
				}

				telemetryFiles.Add(($"Telemetry/{telemetryFile.GetFileName()}", telemetryFile));
			}

			List<TraceEvent> telemetrySpans = new List<TraceEvent>();
			foreach (TraceEventList telemetryEventList in telemetryList.OrderBy(x => x.Spans.First().StartTime).ThenBy(x => x.Spans.Last().FinishTime))
			{
				foreach (TraceEvent span in telemetryEventList.Spans)
				{
					if (span.FinishTime - span.StartTime > TimeSpan.FromMilliseconds(1.0))
					{
						span.Index = telemetrySpans.Count;
						telemetrySpans.Add(span);
					}
				}
			}

			FileReference? traceFile = null;
			if (telemetrySpans.Count > 0)
			{
				TraceSpan rootSpan = new TraceSpan();
				rootSpan.Name = stepName;

				Stack<TraceSpan> stack = new Stack<TraceSpan>();
				stack.Push(rootSpan);

				List<TraceSpan> spansWithExplicitParent = new List<TraceSpan>();
				Dictionary<string, TraceSpan> spans = new Dictionary<string, TraceSpan>(StringComparer.OrdinalIgnoreCase);

				foreach (TraceEvent traceEvent in telemetrySpans.OrderBy(x => x.StartTime).ThenByDescending(x => x.FinishTime).ThenBy(x => x.Index))
				{
					TraceSpan newSpan = new TraceSpan();
					newSpan.Name = traceEvent.Name;
					newSpan.SpanId = traceEvent.SpanId;
					newSpan.ParentId = traceEvent.ParentId;
					newSpan.Service = traceEvent.Service;
					newSpan.Resource = traceEvent.Resource;
					newSpan.Start = traceEvent.StartTime.UtcTicks;
					newSpan.Finish = traceEvent.FinishTime.UtcTicks;
					if (traceEvent.Metadata != null && traceEvent.Metadata.Count > 0)
					{
						newSpan.Properties = traceEvent.Metadata;
					}

					if (!String.IsNullOrEmpty(newSpan.SpanId))
					{
						spans.Add(newSpan.SpanId, newSpan);
					}

					TraceSpan stackTop = stack.Peek();
					while (stack.Count > 1 && newSpan.Start >= stackTop.Finish)
					{
						stack.Pop();
						stackTop = stack.Peek();
					}

					if (String.IsNullOrEmpty(newSpan.ParentId))
					{
						if (stack.Count > 1 && newSpan.Finish > stackTop.Finish)
						{
							jobLogger.LogInformation("Trace event name='{Name}', service'{Service}', resource='{Resource}' has invalid finish time ({SpanFinish} < {StackFinish})", newSpan.Name, newSpan.Service, newSpan.Resource, newSpan.Finish, stackTop.Finish);
							newSpan.Finish = stackTop.Finish;
						}

						stackTop.AddChild(newSpan);
					}
					else
					{
						spansWithExplicitParent.Add(newSpan);
					}

					stack.Push(newSpan);
				}

				foreach (TraceSpan span in spansWithExplicitParent)
				{
					if (span.ParentId != null && spans.TryGetValue(span.ParentId, out TraceSpan? parentSpan))
					{
						parentSpan.AddChild(span);
					}
					else
					{
						Logger.LogInformation("Parent {ParentId} of span {SpanId} was not found.", span.ParentId, span.SpanId);
					}
				}

				rootSpan.Start = rootSpan.Children!.First().Start;
				rootSpan.Finish = rootSpan.Children!.Last().Finish;

				traceFile = FileReference.Combine(telemetryDir, "Trace.json");
				using (FileStream stream = FileReference.Open(traceFile, FileMode.Create))
				{
					JsonSerializerOptions options = new JsonSerializerOptions { DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull };
					await JsonSerializer.SerializeAsync(stream, rootSpan, options, cancellationToken);
				}
				telemetryFiles.Add(("Trace.json", traceFile));

				CreateTracingData(buildGraphSpan, rootSpan);
			}
		}

		private async Task CreateReportAsync(JobStepId stepId, FileReference reportFile, ILogger logger)
		{
			byte[] data = await FileReference.ReadAllBytesAsync(reportFile);

			JsonSerializerOptions options = new JsonSerializerOptions();
			options.PropertyNameCaseInsensitive = true;
			options.Converters.Add(new JsonStringEnumConverter());

			ReportData report = JsonSerializer.Deserialize<ReportData>(data, options)!;
			if (String.IsNullOrEmpty(report.Name))
			{
				logger.LogWarning("Missing 'Name' field in report data");
				return;
			}

			if (String.IsNullOrEmpty(report.Content) && !String.IsNullOrEmpty(report.FileName))
			{
				FileReference reportDataFile = FileReference.Combine(reportFile.Directory, report.FileName);
				if (!FileReference.Exists(reportDataFile))
				{
					logger.LogWarning("Cannot find file '{File}' referenced by report data", report.FileName);
					return;
				}
				report.Content = await FileReference.ReadAllTextAsync(reportDataFile);
			}

			RpcCreateReportRequest request = new RpcCreateReportRequest();
			request.JobId = JobId.ToString();
			request.BatchId = BatchId.ToString();
			request.StepId = stepId.ToString();
			request.Scope = report.Scope;
			request.Placement = report.Placement;
			request.Name = report.Name;
			request.Content = report.Content;
			await JobRpc.CreateReportAsync(request);
		}

		private TelemetrySpan CreateTracingData(TelemetrySpan parent, TraceSpan span)
		{
			TelemetrySpan newSpan = Tracer.StartSpan(span.Name ?? "TraceJsonUnknown", SpanKind.Internal, parent, startTime: new DateTime(span.Start, DateTimeKind.Utc));
			newSpan.SetAttribute("service.name", span.Service);
			newSpan.SetAttribute("resource.name", span.Resource);

			if (span.Properties != null)
			{
				foreach (KeyValuePair<string, string> pair in span.Properties)
				{
					newSpan.SetAttribute(pair.Key, pair.Value);
				}
			}
			if (span.Children != null)
			{
				foreach (TraceSpan child in span.Children)
				{
					CreateTracingData(newSpan, child);
				}
			}

			newSpan.End(new DateTime(span.Finish, DateTimeKind.Utc));
			return newSpan;
		}
		
		private static void DecorateSpanWithArtifact(TelemetrySpan span, string prefix, RpcCreateJobArtifactRequestV2 request, RpcCreateJobArtifactResponseV2 response)
		{
			span.SetAttribute(prefix + "artifact.name", request.Name);
			span.SetAttribute(prefix + "artifact.type", request.Type);
			span.SetAttribute(prefix + "artifact.job_id", request.JobId);
			span.SetAttribute(prefix + "artifact.step_id", request.StepId);
			
			span.SetAttribute(prefix + "artifact.id", response.Id);
			span.SetAttribute(prefix + "artifact.ref_name", response.RefName);
			span.SetAttribute(prefix + "artifact.namespace_id", response.NamespaceId);
		}

		public async Task UploadTestDataAsync(JobStepId jobStepId, IEnumerable<KeyValuePair<string, object>> pairs)
		{
			if (pairs.Any())
			{
				using (AsyncClientStreamingCall<RpcUploadTestDataRequest, RpcUploadTestDataResponse> call = JobRpc.UploadTestData())
				{
					foreach (KeyValuePair<string, object> pair in pairs)
					{
						JsonSerializerOptions options = new JsonSerializerOptions();
						options.PropertyNameCaseInsensitive = true;
						options.Converters.Add(new JsonStringEnumConverter());
						byte[] data = JsonSerializer.SerializeToUtf8Bytes(pair.Value, options);

						RpcUploadTestDataRequest request = new RpcUploadTestDataRequest();
						request.JobId = JobId.ToString();
						request.JobStepId = jobStepId.ToString();
						request.Key = pair.Key;
						request.Value = Google.Protobuf.ByteString.CopyFrom(data);
						await call.RequestStream.WriteAsync(request);
					}
					await call.RequestStream.CompleteAsync();
					await call.ResponseAsync;
				}
			}
		}
	}

	interface IJobExecutorFactory
	{
		string Name { get; }

		Task<JobExecutor> CreateExecutorAsync(RpcAgentWorkspace workspaceInfo, RpcAgentWorkspace? autoSdkWorkspaceInfo, JobExecutorOptions options, CancellationToken cancellationToken);
	}

	static class JobExecutorHelpers
	{
		public static async Task ExecuteAsync(IHordeClient hordeClient, DirectoryReference workingDir, LeaseId leaseId, ExecuteJobTask executeTask, IEnumerable<IJobExecutorFactory> executorFactories, DriverSettings driverSettings, ILogger logger, CancellationToken cancellationToken)
		{
			// Create an executor for this job
			string executorName = String.IsNullOrEmpty(executeTask.JobOptions.Executor) ? driverSettings.Executor : executeTask.JobOptions.Executor;

			IJobExecutorFactory? executorFactory = executorFactories.FirstOrDefault(x => x.Name.Equals(executorName, StringComparison.OrdinalIgnoreCase));
			if (executorFactory == null)
			{
				throw new InvalidOperationException($"Unable to find executor '{executorName}'");
			}

			JobId jobId = JobId.Parse(executeTask.JobId);
			JobStepBatchId batchId = JobStepBatchId.Parse(executeTask.BatchId);

			JobExecutorOptions options = new JobExecutorOptions(hordeClient, workingDir, driverSettings.ProcessesToTerminate, jobId, batchId, leaseId, executeTask.JobOptions);

			using JobExecutor executor = await executorFactory.CreateExecutorAsync(executeTask.Workspace, executeTask.AutoSdkWorkspace, options, cancellationToken);
			await executor.ExecuteAsync(logger, cancellationToken);
		}
	}
}
