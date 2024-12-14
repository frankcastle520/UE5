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
	class ConformHandler : LeaseHandler<ConformTask>
	{
		public ConformHandler(RpcLease lease)
			: base(lease)
		{ }

		/// <inheritdoc/>
		protected override async Task<LeaseResult> ExecuteAsync(ISession session, LeaseId leaseId, ConformTask conformTask, Tracer tracer, ILogger localLogger, CancellationToken cancellationToken)
		{
			await using IServerLogger serverLogger = session.HordeClient.CreateServerLogger(LogId.Parse(conformTask.LogId)).WithLocalLogger(localLogger);

			List<string> arguments = new List<string>();
			arguments.Add("execute");
			arguments.Add("conform");
			arguments.Add($"-AgentId={session.AgentId}");
			arguments.Add($"-SessionId={session.SessionId}");
			arguments.Add($"-LeaseId={leaseId}");
			arguments.Add($"-WorkingDir={session.WorkingDir}");
			arguments.Add($"-Task={Convert.ToBase64String(conformTask.ToByteArray())}");

			FileReference driverAssembly = FileReference.Combine(new DirectoryReference(AppContext.BaseDirectory), "JobDriver", "JobDriver.dll");

			Dictionary<string, string> environment = ManagedProcess.GetCurrentEnvVars();
			environment[HordeHttpClient.HordeUrlEnvVarName] = session.HordeClient.ServerUrl.ToString();
			environment[HordeHttpClient.HordeTokenEnvVarName] = await session.HordeClient.GetAccessTokenAsync(false, cancellationToken) ?? String.Empty;
			environment["UE_LOG_JSON_TO_STDOUT"] = "1";

			int exitCode = await RunDotNetProcessAsync(driverAssembly, arguments, environment, AgentApp.IsSelfContained, serverLogger, cancellationToken);
			serverLogger.LogInformation("Driver finished with exit code {ExitCode}", exitCode);

			return (exitCode == 0) ? LeaseResult.Success : LeaseResult.Failed;
		}
	}

	class ConformHandlerFactory : LeaseHandlerFactory<ConformTask>
	{
		public override LeaseHandler<ConformTask> CreateHandler(RpcLease lease)
			=> new ConformHandler(lease);
	}
}

