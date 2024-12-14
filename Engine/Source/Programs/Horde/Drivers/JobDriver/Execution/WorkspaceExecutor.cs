// Copyright Epic Games, Inc. All Rights Reserved.

using System.Text.RegularExpressions;
using EpicGames.Core;
using JobDriver.Utility;
using Horde.Common.Rpc;
using HordeCommon.Rpc.Messages;
using Microsoft.Extensions.Logging;
using OpenTelemetry.Trace;

namespace JobDriver.Execution
{
	class WorkspaceExecutor : JobExecutor
	{
		public const string Name = "Workspace";

		private DirectoryReference? _sharedStorageDir;
		private readonly IWorkspaceMaterializer _workspace;
		private readonly IWorkspaceMaterializer? _autoSdkWorkspace;

		public WorkspaceExecutor(JobExecutorOptions options, IWorkspaceMaterializer workspace, IWorkspaceMaterializer? autoSdkWorkspace, Tracer tracer, ILogger logger)
			: base(options, tracer, logger)
		{
			_workspace = workspace;
			_autoSdkWorkspace = autoSdkWorkspace;
		}

		protected override void Dispose(bool disposing)
		{
			if (disposing)
			{
				_workspace.Dispose();
				_autoSdkWorkspace?.Dispose();
			}

			base.Dispose(disposing);
		}

		public override async Task InitializeAsync(RpcBeginBatchResponse batch, ILogger logger, CancellationToken cancellationToken)
		{
			using TelemetrySpan span = Tracer.StartActiveSpan($"{nameof(WorkspaceExecutor)}.{nameof(InitializeAsync)}");
			await base.InitializeAsync(batch, logger, cancellationToken);

			if (Batch.Change == 0)
			{
				throw new WorkspaceMaterializationException("Jobs with an empty change number are not supported");
			}

			// Setup and sync the AutoSDK workspace
			if (_autoSdkWorkspace != null)
			{
				using TelemetrySpan autoSdkSpan = Tracer.StartActiveSpan("Workspace");
				autoSdkSpan.SetAttribute("resource.name", "AutoSDK");
				// TODO: Set type of workspace materializer as scope tag.

				SyncOptions syncOptions = new();
				await _autoSdkWorkspace.SyncAsync(IWorkspaceMaterializer.LatestChangeNumber, -1, syncOptions, cancellationToken);
			}

			// Sync the regular workspace
			{
				using TelemetrySpan autoSdkSpan = Tracer.StartActiveSpan("Workspace");
				autoSdkSpan.SetAttribute("resource.name", _workspace.Identifier);

				await _workspace.SyncAsync(Batch.Change, Batch.PreflightChange, new SyncOptions(), cancellationToken);

				// TODO: Purging of cache for ManagedWorkspace did happen here in WorkspaceInfo

				DeleteCachedBuildGraphManifests(_workspace.DirectoryPath, logger);
			}

			// Remove all the local settings directories
			PerforceExecutor.DeleteEngineUserSettings(logger);

			// Get the temp storage directory
			if (!String.IsNullOrEmpty(Batch.TempStorageDir))
			{
				string escapedStreamName = Regex.Replace(Batch.StreamName, "[^a-zA-Z0-9_-]", "+");
				_sharedStorageDir = DirectoryReference.Combine(new DirectoryReference(Batch.TempStorageDir), escapedStreamName, $"CL {Batch.Change} - Job {JobId}");
				CopyAutomationTool(_sharedStorageDir, _workspace.DirectoryPath, logger);
			}

			// Set any non-materializer specific environment variables for jobs
			_envVars["IsBuildMachine"] = "1";
			_envVars["uebp_LOCAL_ROOT"] = _workspace.DirectoryPath.FullName;
			_envVars["uebp_BuildRoot_P4"] = Batch.StreamName;
			_envVars["uebp_BuildRoot_Escaped"] = Batch.StreamName.Replace('/', '+');
			_envVars["uebp_CL"] = Batch.Change.ToString();
			_envVars["uebp_CodeCL"] = Batch.CodeChange.ToString();

			foreach ((string key, string value) in _workspace.EnvironmentVariables)
			{
				_envVars[key] = value;
			}

			if (_autoSdkWorkspace != null)
			{
				_envVars["UE_SDKS_ROOT"] = _autoSdkWorkspace.DirectoryPath.FullName;
			}
		}

		/// <inheritdoc/>
		protected override async Task<bool> SetupAsync(JobStepInfo step, ILogger logger, CancellationToken cancellationToken)
		{
			// Loop back to JobExecutor's SetupAsync again, but with workspace and shared storage dir set
			DirectoryReference workspaceDir = _workspace.DirectoryPath;
			return await SetupAsync(step, workspaceDir, _workspace.IsPerforceWorkspace, GetLogger(logger), cancellationToken);
		}

		/// <inheritdoc/>
		protected override async Task<bool> ExecuteAsync(JobStepInfo step, ILogger logger, CancellationToken cancellationToken)
		{
			// Loop back to JobExecutor's ExecuteAsync again, but with workspace and shared storage dir set
			DirectoryReference workspaceDir = _workspace.DirectoryPath;
			return await ExecuteAsync(step, workspaceDir, _workspace.IsPerforceWorkspace, GetLogger(logger), cancellationToken);
		}

		/// <inheritdoc/>
		public override async Task FinalizeAsync(ILogger logger, CancellationToken cancellationToken)
		{
			DirectoryReference workspaceDir = _workspace.DirectoryPath;
			await ExecuteLeaseCleanupScriptAsync(workspaceDir, logger);
			await TerminateProcessesAsync(TerminateCondition.AfterBatch, logger);

			if (_autoSdkWorkspace != null)
			{
				await _autoSdkWorkspace.FinalizeAsync(cancellationToken);
			}

			await _workspace.FinalizeAsync(cancellationToken);
		}

		private ILogger GetLogger(ILogger logger)
		{
			if (_workspace.IsPerforceWorkspace)
			{
				// Try resolve a PerforceLogger using assumptions about the materializer.
				// This is to remain compatible with PerforceExecutor.
				// These Perforce-specific references should ideally not exist in WorkspaceExecutor.
				WorkspaceInfo? workspaceInfo = (_workspace as ManagedWorkspaceMaterializer)?.GetWorkspaceInfo();
				WorkspaceInfo? autoSdkWorkspaceInfo = (_autoSdkWorkspace as ManagedWorkspaceMaterializer)?.GetWorkspaceInfo();

				if (workspaceInfo != null)
				{
					return PerforceExecutor.CreatePerforceLogger(logger, Batch.Change, workspaceInfo, autoSdkWorkspaceInfo);
				}
			}

			return logger;
		}
	}

	class WorkspaceExecutorFactory : IJobExecutorFactory
	{
		private readonly IEnumerable<IWorkspaceMaterializerFactory> _materializerFactories;
		private readonly Tracer _tracer;
		private readonly ILoggerFactory _loggerFactory;

		public string Name => WorkspaceExecutor.Name;

		public WorkspaceExecutorFactory(IEnumerable<IWorkspaceMaterializerFactory> materializerFactories, Tracer tracer, ILoggerFactory loggerFactory)
		{
			_materializerFactories = materializerFactories;
			_tracer = tracer;
			_loggerFactory = loggerFactory;
		}

		public async Task<JobExecutor> CreateExecutorAsync(RpcAgentWorkspace workspaceInfo, RpcAgentWorkspace? autoSdkWorkspaceInfo, JobExecutorOptions options, CancellationToken cancellationToken)
		{
			IWorkspaceMaterializer? workspaceMaterializer = null;
			IWorkspaceMaterializer? autoSdkMaterializer = null;
			try
			{
				string name = String.IsNullOrEmpty(options.JobOptions.WorkspaceMaterializer) ? ManagedWorkspaceMaterializer.Name : options.JobOptions.WorkspaceMaterializer;

				workspaceMaterializer = await CreateMaterializerAsync(name, workspaceInfo, options.WorkingDir, forAutoSdk: false, cancellationToken);
				if (autoSdkWorkspaceInfo != null)
				{
					autoSdkMaterializer = await CreateMaterializerAsync(name, autoSdkWorkspaceInfo, options.WorkingDir, forAutoSdk: true, cancellationToken);
				}

				return new WorkspaceExecutor(options, workspaceMaterializer, autoSdkMaterializer, _tracer, _loggerFactory.CreateLogger<WorkspaceExecutor>());
			}
			catch
			{
				autoSdkMaterializer?.Dispose();
				workspaceMaterializer?.Dispose();
				throw;
			}
		}

		async Task<IWorkspaceMaterializer> CreateMaterializerAsync(string name, RpcAgentWorkspace workspaceInfo, DirectoryReference workspaceDir, bool forAutoSdk, CancellationToken cancellationToken)
		{
			foreach (IWorkspaceMaterializerFactory materializerFactory in _materializerFactories)
			{
				IWorkspaceMaterializer? materializer = await materializerFactory.CreateMaterializerAsync(name, workspaceInfo, workspaceDir, forAutoSdk, cancellationToken);
				if (materializer != null)
				{
					return materializer;
				}
			}
			throw new ArgumentException($"Unable to find materializer type '{name}'");
		}
	}
}
