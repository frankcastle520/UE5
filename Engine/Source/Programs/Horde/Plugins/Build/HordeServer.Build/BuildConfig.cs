// Copyright Epic Games, Inc. All Rights Reserved.

using System.ComponentModel.DataAnnotations;
using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using System.Security.Claims;
using System.Text.Json.Serialization;
using EpicGames.Horde.Acls;
using EpicGames.Horde.Agents.Pools;
using EpicGames.Horde.Artifacts;
using EpicGames.Horde.Common;
using EpicGames.Horde.Jobs.Templates;
using EpicGames.Horde.Projects;
using EpicGames.Horde.Streams;
using EpicGames.Perforce;
using HordeServer.Acls;
using HordeServer.Agents;
using HordeServer.Agents.Pools;
using HordeServer.Artifacts;
using HordeServer.Commits;
using HordeServer.Devices;
using HordeServer.Perforce;
using HordeServer.Plugins;
using HordeServer.Projects;
using HordeServer.Streams;
using HordeServer.Utilities;

#pragma warning disable CA2227 // Change x to be read-only by removing the property setter

namespace HordeServer
{
	/// <summary>
	/// Configuration for the build plugin
	/// </summary>
	public class BuildConfig : IPluginConfig
	{
		/// <summary>
		/// Acl of this scope
		/// </summary>
		[JsonIgnore]
		public AclConfig Acl { get; private set; } = null!;

		/// <summary>
		/// List of Perforce clusters
		/// </summary>
		public List<PerforceCluster> PerforceClusters { get; set; } = new List<PerforceCluster>();

		/// <summary>
		/// Device configuration
		/// </summary>
		public DeviceConfig? Devices { get; set; }

		/// <summary>
		/// Maximum number of conforms to run at once
		/// </summary>
		public int MaxConformCount { get; set; }

		/// <summary>
		/// Time to wait before shutting down an agent that has been disabled
		/// Used if no value is set on the actual pool.
		/// </summary>
		public TimeSpan? AgentShutdownIfDisabledGracePeriod { get; set; } = null;

		/// <summary>
		/// Configuration for different artifact types
		/// </summary>
		public List<ArtifactTypeConfig> ArtifactTypes { get; set; } = new List<ArtifactTypeConfig>();

		/// <summary>
		/// List of projects
		/// </summary>
		public List<ProjectConfig> Projects { get; set; } = new List<ProjectConfig>();

		/// <summary>
		/// Enumerates all the streams
		/// </summary>
		[JsonIgnore]
		public IReadOnlyList<StreamConfig> Streams { get; private set; } = null!;

		/// <summary>
		/// Whether to allow conform tasks to run
		/// </summary>
		public bool EnableConformTasks { get; set; } = true;

		/// <summary>
		/// Commit tag to use for marking issues as fixed
		/// </summary>
		public string IssueFixedTag { get; set; } = "#horde";

		private readonly Dictionary<ProjectId, ProjectConfig> _projectLookup = new Dictionary<ProjectId, ProjectConfig>();
		private readonly Dictionary<StreamId, StreamConfig> _streamLookup = new Dictionary<StreamId, StreamConfig>();

		/// <inheritdoc/>
		public void PostLoad(PluginConfigOptions configOptions)
		{
			Acl = configOptions.ParentAcl;
			Streams = Projects.SelectMany(x => x.Streams).ToList();

			_projectLookup.Clear();
			_streamLookup.Clear();
			foreach (ProjectConfig project in Projects)
			{
				_projectLookup.Add(project.Id, project);
				project.PostLoad(project.Id, configOptions.ParentAcl, ArtifactTypes);

				foreach (StreamConfig stream in project.Streams)
				{
					_streamLookup.Add(stream.Id, stream);
				}
			}

			foreach (ArtifactTypeConfig artifactTypeConfig in ArtifactTypes)
			{
				artifactTypeConfig.PostLoad(Acl);
			}

			UpdateWorkspacesForPools(configOptions.Plugins);
		}

		void UpdateWorkspacesForPools(IEnumerable<IPluginConfig> plugins)
		{
			// Try to get the compute config, and skip if it isn't configured
			ComputeConfig computeConfig = plugins.OfType<ComputeConfig>().First();

			// Lookup table of pool id to workspaces
			Dictionary<PoolId, AutoSdkConfig> poolToAutoSdkView = new Dictionary<PoolId, AutoSdkConfig>();
			Dictionary<PoolId, List<AgentWorkspaceInfo>> poolToAgentWorkspaces = new Dictionary<PoolId, List<AgentWorkspaceInfo>>();

			// Populate the workspace list from the current stream
			foreach (StreamConfig streamConfig in Streams)
			{
				foreach (KeyValuePair<string, AgentConfig> agentTypePair in streamConfig.AgentTypes)
				{
					// Create the new agent workspace
					if (streamConfig.TryGetAgentWorkspace(agentTypePair.Value, out AgentWorkspaceInfo? agentWorkspace, out AutoSdkConfig? autoSdkConfig))
					{
						AgentConfig agentType = agentTypePair.Value;

						// Find or add a list of workspaces for this pool
						List<AgentWorkspaceInfo>? agentWorkspaces;
						if (!poolToAgentWorkspaces.TryGetValue(agentType.Pool, out agentWorkspaces))
						{
							agentWorkspaces = new List<AgentWorkspaceInfo>();
							poolToAgentWorkspaces.Add(agentType.Pool, agentWorkspaces);
						}

						// Add it to the list
						if (!agentWorkspaces.Contains(agentWorkspace))
						{
							agentWorkspaces.Add(agentWorkspace);
						}
						if (autoSdkConfig != null)
						{
							AutoSdkConfig? existingAutoSdkConfig;
							poolToAutoSdkView.TryGetValue(agentType.Pool, out existingAutoSdkConfig);
							poolToAutoSdkView[agentType.Pool] = AutoSdkConfig.Merge(autoSdkConfig, existingAutoSdkConfig);
						}
					}
				}
			}

			// Update the list of workspaces for each pool
			foreach (PoolConfig pool in computeConfig.Pools)
			{
				// Get the new list of workspaces for this pool
				List<AgentWorkspaceInfo>? newWorkspaces;
				if (!poolToAgentWorkspaces.TryGetValue(pool.Id, out newWorkspaces))
				{
					newWorkspaces = new List<AgentWorkspaceInfo>();
				}

				// Get the autosdk view
				AutoSdkConfig? newAutoSdkConfig;
				if (!poolToAutoSdkView.TryGetValue(pool.Id, out newAutoSdkConfig))
				{
					newAutoSdkConfig = AutoSdkConfig.None;
				}

				pool.Workspaces = newWorkspaces;
				pool.AutoSdkConfig = newAutoSdkConfig;
			}
		}

		/// <summary>
		/// Authorize an action to perform on an artifact
		/// </summary>
		public bool AuthorizeArtifact(ArtifactType type, StreamId streamId, AclAction action, ClaimsPrincipal user)
		{
			if (user.HasAdminClaim())
			{
				return true;
			}

			bool? auth = null;
			if (TryGetStream(streamId, out StreamConfig? streamConfig))
			{
				auth = AuthorizeArtifactType(streamConfig.ArtifactTypes, type, action, user);
				auth ??= streamConfig.Acl.AuthorizeSingleScope(action, user);

				ProjectConfig projectConfig = streamConfig.ProjectConfig;

				auth ??= AuthorizeArtifactType(projectConfig.ArtifactTypes, type, action, user);
				auth ??= projectConfig.Acl.AuthorizeSingleScope(action, user);
			}

			auth ??= AuthorizeArtifactType(ArtifactTypes, type, action, user);
			return auth ?? Acl.Authorize(action, user);
		}

		static bool? AuthorizeArtifactType(IReadOnlyList<ArtifactTypeConfig> artifactTypes, ArtifactType artifactType, AclAction action, ClaimsPrincipal user)
		{
			ArtifactTypeConfig? config = artifactTypes.FirstOrDefault(x => x.Type == artifactType);
			return config?.Acl?.AuthorizeSingleScope(action, user);
		}

		/// <summary>
		/// Attempts to get configuration for a project from this object
		/// </summary>
		/// <param name="projectId">The stream identifier</param>
		/// <param name="config">Configuration for the stream</param>
		/// <returns>True if the stream configuration was found</returns>
		public bool TryGetProject(ProjectId projectId, [NotNullWhen(true)] out ProjectConfig? config) => _projectLookup.TryGetValue(projectId, out config);

		/// <summary>
		/// Get configuration for a stream from this object
		/// </summary>
		/// <param name="streamId">The stream identifier</param>
		/// <returns>Configuration for the stream</returns>
		public StreamConfig GetStream(StreamId streamId)
		{
			StreamConfig? streamConfig;
			if (!TryGetStream(streamId, out streamConfig))
			{
				throw new StreamNotFoundException(streamId);
			}
			return streamConfig;
		}

		/// <summary>
		/// Attempts to get configuration for a stream from this object
		/// </summary>
		/// <param name="streamId">The stream identifier</param>
		/// <param name="config">Configuration for the stream</param>
		/// <returns>True if the stream configuration was found</returns>
		public bool TryGetStream(StreamId streamId, [NotNullWhen(true)] out StreamConfig? config) => _streamLookup.TryGetValue(streamId, out config);

		/// <summary>
		/// Attempts to get configuration for a stream from this object
		/// </summary>
		/// <param name="streamId">The stream identifier</param>
		/// <param name="templateId">Template identifier</param>
		/// <param name="config">Configuration for the template</param>
		/// <returns>True if the template configuration was found</returns>
		public bool TryGetTemplate(StreamId streamId, TemplateId templateId, [NotNullWhen(true)] out TemplateRefConfig? config)
		{
			if (!_streamLookup.TryGetValue(streamId, out StreamConfig? streamConfig))
			{
				config = null;
				return false;
			}
			return streamConfig.TryGetTemplate(templateId, out config);
		}

		/// <inheritdoc cref="AclConfig.Authorize"/>
		public bool Authorize(AclAction action, ClaimsPrincipal principal)
			=> Acl.Authorize(action, principal);

		/// <summary>
		/// Finds a perforce cluster with the given name or that contains the provided server
		/// </summary>
		/// <param name="name">Name of the cluster</param>
		/// <param name="serverAndPort">Find cluster which contains server</param>
		/// <returns></returns>
		public PerforceCluster GetPerforceCluster(string? name, string? serverAndPort = null)
		{
			PerforceCluster? cluster = FindPerforceCluster(name, serverAndPort);
			if (cluster == null)
			{
				throw new Exception($"Unknown Perforce cluster '{name}'");
			}
			return cluster;
		}

		/// <summary>
		/// Finds a perforce cluster with the given name or that contains the provided server
		/// </summary>
		/// <param name="name">Name of the cluster</param>
		/// <param name="serverAndPort">Find cluster which contains server</param>
		/// <returns></returns>
		public PerforceCluster? FindPerforceCluster(string? name, string? serverAndPort = null)
		{
			List<PerforceCluster> clusters = PerforceClusters;

			if (serverAndPort != null)
			{
				return clusters.FirstOrDefault(x => x.Servers.FirstOrDefault(server => String.Equals(server.ServerAndPort, serverAndPort, StringComparison.OrdinalIgnoreCase)) != null);
			}

			if (clusters.Count == 0)
			{
				clusters = DefaultClusters;
			}

			if (name == null)
			{
				return clusters.FirstOrDefault();
			}
			else
			{
				return clusters.FirstOrDefault(x => String.Equals(x.Name, name, StringComparison.OrdinalIgnoreCase));
			}
		}

		static List<PerforceCluster> DefaultClusters { get; } = GetDefaultClusters();

		static List<PerforceCluster> GetDefaultClusters()
		{
			PerforceServer server = new PerforceServer();
			server.ServerAndPort = PerforceSettings.Default.ServerAndPort;

			PerforceCluster cluster = new PerforceCluster();
			cluster.Name = "Default";
			cluster.CanImpersonate = false;
			cluster.Servers.Add(server);

			return new List<PerforceCluster> { cluster };
		}
	}

	/// <summary>
	/// Path to a platform and stream to use for syncing AutoSDK
	/// </summary>
	public class AutoSdkWorkspace
	{
		/// <summary>
		/// Name of this workspace
		/// </summary>
		public string? Name { get; set; }

		/// <summary>
		/// The agent properties to check (eg. "OSFamily=Windows")
		/// </summary>
		public List<string> Properties { get; set; } = new List<string>();

		/// <summary>
		/// Username for logging in to the server
		/// </summary>
		public string? UserName { get; set; }

		/// <summary>
		/// Stream to use
		/// </summary>
		[Required]
		public string? Stream { get; set; }
	}

	/// <summary>
	/// Information about an individual Perforce server
	/// </summary>
	public class PerforceServer
	{
		/// <summary>
		/// The server and port. The server may be a DNS entry with multiple records, in which case it will be actively load balanced.
		/// If "ssl:" prefix is used, ensure P4 server's fingerprint/certificate is trusted.
		/// See Horde's documentation on connecting to SSL-enabled Perforce servers.
		/// </summary>
		public string ServerAndPort { get; set; } = "perforce:1666";

		/// <summary>
		/// Whether to query the healthcheck address under each server
		/// </summary>
		public bool HealthCheck { get; set; }

		/// <summary>
		/// Whether to resolve the DNS entries and load balance between different hosts
		/// </summary>
		public bool ResolveDns { get; set; }

		/// <summary>
		/// Maximum number of simultaneous conforms on this server
		/// </summary>
		public int MaxConformCount { get; set; }

		/// <summary>
		/// Optional condition for a machine to be eligible to use this server
		/// </summary>
		public Condition? Condition { get; set; }

		/// <summary>
		/// List of properties for an agent to be eligible to use this server
		/// </summary>
		public List<string>? Properties { get; set; }
	}

	/// <summary>
	/// Credentials for a Perforce user
	/// </summary>
	public class PerforceCredentials
	{
		/// <summary>
		/// The username
		/// </summary>
		public string UserName { get; set; } = String.Empty;

		/// <summary>
		/// Password for the user
		/// </summary>
		public string? Password { get; set; } = String.Empty;

		/// <summary>
		/// Login ticket for the user (will be used instead of password if set)
		/// </summary>
		public string? Ticket { get; set; } = String.Empty;
	}

	/// <summary>
	/// Information about a cluster of Perforce servers. 
	/// </summary>
	[DebuggerDisplay("{Name}")]
	public class PerforceCluster
	{
		/// <summary>
		/// The default cluster name
		/// </summary>
		public const string DefaultName = "Default";

		/// <summary>
		/// Name of the cluster
		/// </summary>
		[Required]
		public string Name { get; set; } = null!;

		/// <summary>
		/// Username for Horde to log in to this server. Will use the first account specified below if not overridden.
		/// </summary>
		public string? ServiceAccount { get; set; }

		/// <summary>
		/// Whether the service account can impersonate other users
		/// </summary>
		public bool CanImpersonate { get; set; } = true;

		/// <summary>
		/// Whether to use partitioned workspaces on this server
		/// </summary>
		public bool SupportsPartitionedWorkspaces { get; set; } = false;

		/// <summary>
		/// List of servers
		/// </summary>
		public List<PerforceServer> Servers { get; set; } = new List<PerforceServer>();

		/// <summary>
		/// List of server credentials
		/// </summary>
		public List<PerforceCredentials> Credentials { get; set; } = new List<PerforceCredentials>();

		/// <summary>
		/// List of autosdk streams
		/// </summary>
		public List<AutoSdkWorkspace> AutoSdk { get; set; } = new List<AutoSdkWorkspace>();
	}
}
