// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Horde.Agents.Pools;
using HordeServer.Agents;
using HordeServer.Agents.Pools;
using HordeServer.Utilities;
using Microsoft.AspNetCore.Authorization;
using Microsoft.AspNetCore.Mvc;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Options;

namespace HordeServer.Server
{
	/// <summary>
	/// Controller managing pools-related server commands
	/// </summary>
	[ApiController]
	[Authorize]
	[Route("[controller]")]
	public class PoolsServerController : HordeControllerBase
	{
		readonly IServiceProvider _serviceProvider;
		readonly IOptionsSnapshot<ComputeConfig> _computeConfig;

		/// <summary>
		/// Constructor
		/// </summary>
		public PoolsServerController(IServiceProvider serviceProvider, IOptionsSnapshot<ComputeConfig> computeConfig)
		{
			_serviceProvider = serviceProvider;
			_computeConfig = computeConfig;
		}

		/// <summary>
		/// Converts all legacy pools into config entries
		/// </summary>
		[HttpGet]
		[Route("/api/v1/server/migrate/pool-config")]
		public async Task<ActionResult<object>> MigratePoolsAsync([FromQuery] int? minAgents = null, [FromQuery] int? maxAgents = null, CancellationToken cancellationToken = default)
		{
			if (!_computeConfig.Value.Authorize(PoolAclAction.ListPools, User))
			{
				return Forbid(PoolAclAction.ListPools);
			}

			IPoolCollection poolCollection = _serviceProvider.GetRequiredService<IPoolCollection>();
			List<IPoolConfig> poolConfigs = (await poolCollection.GetConfigsAsync(cancellationToken)).ToList();
			HashSet<PoolId> removePoolIds = _computeConfig.Value.Pools.Select(x => x.Id).ToHashSet();
			poolConfigs.RemoveAll(x => removePoolIds.Contains(x.Id));

			if (minAgents != null || maxAgents != null)
			{
				IAgentCollection agentCollection = _serviceProvider.GetRequiredService<IAgentCollection>();

				Dictionary<PoolId, int> poolIdToCount = new Dictionary<PoolId, int>();

				await foreach (IAgent agent in agentCollection.FindAsync(cancellationToken: cancellationToken))
				{
					foreach (PoolId poolId in agent.Pools)
					{
						int count;
						if (!poolIdToCount.TryGetValue(poolId, out count))
						{
							count = 0;
						}
						poolIdToCount[poolId] = count + 1;
					}
				}

				if (minAgents != null && minAgents.Value > 0)
				{
					poolConfigs.RemoveAll(x => !poolIdToCount.TryGetValue(x.Id, out int count) || count < minAgents.Value);
				}
				if (maxAgents != null)
				{
					poolConfigs.RemoveAll(x => poolIdToCount.TryGetValue(x.Id, out int count) && count > maxAgents.Value);
				}
			}

			List<PoolConfig> configs = new List<PoolConfig>();
			foreach (IPoolConfig currentConfig in poolConfigs.OrderBy(x => x.Id.Id.Text))
			{
				PoolConfig config = new PoolConfig();
				config.Id = currentConfig.Id;
				config.Name = currentConfig.Name;
				config.Condition = currentConfig.Condition;
				if (currentConfig.Properties != null && currentConfig.Properties.Count > 0 && (currentConfig.Properties.Count != 0 && (currentConfig.Properties.First().Key != "color" && currentConfig.Properties.First().Value != "0")))
				{
					config.Properties = new Dictionary<string, string>(currentConfig.Properties);
				}
				config.EnableAutoscaling = currentConfig.EnableAutoscaling;
				config.MinAgents = currentConfig.MinAgents;
				config.NumReserveAgents = currentConfig.NumReserveAgents;
				config.ConformInterval = currentConfig.ConformInterval;
				config.ScaleInCooldown = currentConfig.ScaleInCooldown;
				config.ScaleOutCooldown = currentConfig.ScaleOutCooldown;
				config.ShutdownIfDisabledGracePeriod = currentConfig.ShutdownIfDisabledGracePeriod;
#pragma warning disable CS0618 // Type or member is obsolete
				config.SizeStrategy = currentConfig.SizeStrategy;
#pragma warning restore CS0618 // Type or member is obsolete
				if (currentConfig.SizeStrategies != null && currentConfig.SizeStrategies.Count > 0)
				{
					config.SizeStrategies = currentConfig.SizeStrategies.ToList();
				}
				if (currentConfig.FleetManagers != null && currentConfig.FleetManagers.Count > 0)
				{
					config.FleetManagers = currentConfig.FleetManagers.ToList();
				}
				config.LeaseUtilizationSettings = currentConfig.LeaseUtilizationSettings;
				config.JobQueueSettings = currentConfig.JobQueueSettings;
				config.ComputeQueueAwsMetricSettings = currentConfig.ComputeQueueAwsMetricSettings;
				configs.Add(config);
			}

			return new { Pools = configs };
		}
	}
}
