// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Horde.Dashboard;
using HordeServer.Plugins;
using Microsoft.AspNetCore.Http;
using Microsoft.Extensions.Options;

namespace HordeServer
{
	class BuildResponseFilter : IPluginResponseFilter
	{
		readonly BuildServerConfig _staticBuildConfig;

		public BuildResponseFilter(IOptions<BuildServerConfig> staticBuildConfig)
		{
			_staticBuildConfig = staticBuildConfig.Value;
		}

		/// <inheritdoc/>
		public void Apply(HttpContext context, object response)
		{
			if (response is GetDashboardConfigResponse dashboardConfigResponse)
			{
				if (_staticBuildConfig.JiraUrl != null)
				{
					dashboardConfigResponse.ExternalIssueServiceName = "Jira";
					dashboardConfigResponse.ExternalIssueServiceUrl = _staticBuildConfig.JiraUrl.ToString().TrimEnd('/');
				}

				if (_staticBuildConfig.P4SwarmUrl != null)
				{
					dashboardConfigResponse.PerforceSwarmUrl = _staticBuildConfig.P4SwarmUrl.ToString().TrimEnd('/');
				}

				dashboardConfigResponse.DeviceProblemCooldownMinutes = _staticBuildConfig.DeviceProblemCooldownMinutes;
			}
		}
	}
}
