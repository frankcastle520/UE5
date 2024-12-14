// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Agents;
using HordeServer.Compute;
using HordeServer.Logs;
using HordeServer.Storage;
using Microsoft.Extensions.DependencyInjection;

namespace HordeServer.Tests
{
	public class ComputeTestSetup : ServerTestSetup
	{
		public ComputeService ComputeService => ServiceProvider.GetRequiredService<ComputeService>();
		public IAgentCollection AgentCollection => ServiceProvider.GetRequiredService<IAgentCollection>();
		public IAgentScheduler AgentScheduler => ServiceProvider.GetRequiredService<IAgentScheduler>();
		public AgentService AgentService => ServiceProvider.GetRequiredService<AgentService>();
		public StorageService StorageService => ServiceProvider.GetRequiredService<StorageService>();
		public ILogCollection LogCollection => ServiceProvider.GetRequiredService<ILogCollection>();

		public ComputeTestSetup()
		{
			AddPlugin<StoragePlugin>();
			AddPlugin<ComputePlugin>();
		}
	}
}
