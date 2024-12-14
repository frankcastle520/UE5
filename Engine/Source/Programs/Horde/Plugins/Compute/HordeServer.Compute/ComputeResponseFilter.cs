// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Horde.Users;
using HordeServer.Agents;
using HordeServer.Agents.Pools;
using HordeServer.Plugins;
using Microsoft.AspNetCore.Http;
using Microsoft.Extensions.Options;

namespace HordeServer
{
	class ComputeResponseFilter : IPluginResponseFilter
	{
		readonly IOptionsMonitor<ComputeConfig> _computeConfig;

		public ComputeResponseFilter(IOptionsMonitor<ComputeConfig> computeConfig)
		{
			_computeConfig = computeConfig;
		}

		public void Apply(HttpContext context, object response)
		{
			if (response is GetDashboardFeaturesResponse featuresResponse)
			{
				ComputeConfig computeConfig = _computeConfig.CurrentValue;
				featuresResponse.ShowPoolEditor = computeConfig.VersionEnum < ConfigVersion.PoolsInConfigFiles && (computeConfig.Authorize(PoolAclAction.CreatePool, context.User) || computeConfig.Authorize(PoolAclAction.UpdatePool, context.User));
				featuresResponse.ShowRemoteDesktop = computeConfig.Authorize(AgentAclAction.UpdateAgent, context.User);
			}
		}
	}
}
