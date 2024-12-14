// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Horde;
using EpicGames.Horde.Agents.Leases;
using EpicGames.Horde.Logs;
using Google.Protobuf;
using HordeAgent.Services;
using HordeCommon.Rpc.Messages;
using HordeCommon.Rpc.Tasks;
using Microsoft.Extensions.Logging;
using OpenTelemetry.Trace;

namespace HordeAgent.Leases.Handlers
{
	class JobHandler : LeaseHandler<ExecuteJobTask>
	{
		public JobHandler(RpcLease lease)
			: base(lease)
		{ }

		/// <inheritdoc/>
		protected override async Task<LeaseResult> ExecuteAsync(ISession session, LeaseId leaseId, ExecuteJobTask executeTask, Tracer tracer, ILogger localLogger, CancellationToken cancellationToken)
		{
			using TelemetrySpan span = tracer.StartActiveSpan($"{nameof(JobHandler)}.{nameof(ExecuteAsync)}");
			span.SetAttribute("horde.job.id", executeTask.JobId);
			span.SetAttribute("horde.job.name", executeTask.JobName);
			span.SetAttribute("horde.job.batch_id", executeTask.BatchId);

			await using IServerLogger logger = session.HordeClient.CreateServerLogger(LogId.Parse(executeTask.LogId)).WithLocalLogger(localLogger);
			try
			{
				executeTask.JobOptions ??= new RpcJobOptions();

				List<string> arguments = new List<string>();
				arguments.Add("execute");
				arguments.Add("job");
				arguments.Add($"-AgentId={session.AgentId}");
				arguments.Add($"-SessionId={session.SessionId}");
				arguments.Add($"-LeaseId={leaseId}");
				arguments.Add($"-WorkingDir={session.WorkingDir}");
				arguments.Add($"-Task={Convert.ToBase64String(executeTask.ToByteArray())}");

				string driverName = String.IsNullOrEmpty(executeTask.JobOptions.Driver) ? "JobDriver" : executeTask.JobOptions.Driver;
				FileReference driverAssembly = FileReference.Combine(new DirectoryReference(AppContext.BaseDirectory), driverName, $"{driverName}.dll");
				span.SetAttribute("horde.job.driver_name", driverName);

				Dictionary<string, string> environment = ManagedProcess.GetCurrentEnvVars();
				environment[HordeHttpClient.HordeUrlEnvVarName] = session.HordeClient.ServerUrl.ToString();
				environment[HordeHttpClient.HordeTokenEnvVarName] = executeTask.Token;
				environment["UE_LOG_JSON_TO_STDOUT"] = "1";

				int exitCode = await RunDotNetProcessAsync(driverAssembly, arguments, environment, AgentApp.IsSelfContained, logger, cancellationToken);
				logger.LogInformation("Driver finished with exit code {ExitCode}", exitCode);

				return (exitCode == 0) ? LeaseResult.Success : LeaseResult.Failed;
			}
			catch (OperationCanceledException ex)
			{
				logger.LogError(ex, "Lease was cancelled ({Reason})", CancellationReason);
				throw;
			}
			catch (Exception ex)
			{
				logger.LogError(ex, "Unhandled exception: {Message}", ex.Message);
				throw;
			}
		}
	}

	class JobHandlerFactory : LeaseHandlerFactory<ExecuteJobTask>
	{
		public override LeaseHandler<ExecuteJobTask> CreateHandler(RpcLease lease)
			=> new JobHandler(lease);
	}
}

