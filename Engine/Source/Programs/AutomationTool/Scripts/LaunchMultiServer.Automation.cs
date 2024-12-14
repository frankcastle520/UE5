// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.IO;
using UnrealBuildBase;
using UnrealBuildTool;
using EpicGames.Core;
using Microsoft.Extensions.Logging;
using System.Collections.Generic;

namespace AutomationTool
{

	// Mirrors FMultiServerDefinition from the MultiServerReplication plugin
	class MultiServerDefinition
	{
		public string LocalId = "0";
		public int ListenPort = 0;

		public override string ToString()
		{
			return "MultiServerDefinition: LocalId \"" + LocalId + "\", ListenPort " + ListenPort;
		}
	}

	[Help("Launches multiple server processes for a project using the MultiServerReplication plugin.")]
	[Help("project=<project>", "Project to open. Will search current path and paths in ueprojectdirs.")]
	[Help("map=<MapName>", "Map to load on startup.")]
	public class LaunchMultiServer : BuildCommand, IProjectParamsHelpers
	{
		public override ExitCode Execute()
		{
			Logger.LogInformation("********** RUN MULTISERVER COMMAND STARTED **********");
			var StartTime = DateTime.UtcNow;

			var MapToRun = ParseParamValue("map", "");

			// Parse server configuration from ini files
			ConfigHierarchy ProjectGameConfig = ConfigCache.ReadHierarchy(ConfigHierarchyType.Game, DirectoryReference.FromFile(ProjectPath), UnrealTargetPlatform.Win64);

			const String ServerDefConfigSection = "/Script/MultiServerConfiguration.MultiServerSettings";

			ProjectGameConfig.TryGetValuesGeneric(ServerDefConfigSection, "ServerDefinitions", out MultiServerDefinition[] ConfigServerDefs);

			if (ConfigServerDefs != null && ConfigServerDefs.Length > 0)
			{
				// Use a dictionary to store ServerDefs by unqiue ID
				Dictionary<string, MultiServerDefinition> UniqueServerDefs = new Dictionary<string, MultiServerDefinition>();

				// Check for duplicates and build peer list
				string PeerList = "";
				for (int i = 0; i < ConfigServerDefs.Length; i++)
				{
					if (UniqueServerDefs.TryAdd(ConfigServerDefs[i].LocalId, ConfigServerDefs[i]))
					{
						PeerList += String.Format("127.0.0.1:{0}{1}", ConfigServerDefs[i].ListenPort, i < (ConfigServerDefs.Length - 1) ? "," : "");
					}
					else
					{
						Logger.LogInformation("MultiServer server defintion with duplicate ID {0}, ignoring duplicate entry {1}", ConfigServerDefs[i].LocalId, ConfigServerDefs[i]);
					}
				}

				var CommonArgs = ProjectPath.FullName + " " + MapToRun + " -server -log -newconsole -MultiServerPeers=" + PeerList;

				var ServerApp = CombinePaths(CmdEnv.LocalRoot, "Engine/Binaries/Win64/UnrealEditor.exe");
				PushDir(Path.GetDirectoryName(ServerApp));

				try
				{
					foreach (var ServerDef in UniqueServerDefs.Values)
					{
						Logger.LogInformation("Starting MultiServer instance with LocalId {0} and port {1}", ServerDef.LocalId, ServerDef.ListenPort);

						var Args = CommonArgs + String.Format(" -MultiServerLocalId={0} -MultiServerListenPort={1}", ServerDef.LocalId, ServerDef.ListenPort);
						var ServerProcess = Run(ServerApp, Args, null, ERunOptions.Default | ERunOptions.NoWaitForExit | ERunOptions.NoStdOutRedirect);

						if (ServerProcess != null)
						{
							// Remove started process so it won't be killed on UAT exit.
							// Essentially forces the -NoKill command-line option behavior for these.
							ProcessManager.RemoveProcess(ServerProcess);
						}
					}
				}
				catch
				{
					throw;
				}
				finally
				{
					PopDir();
				}
			}
			else
			{
				Logger.LogInformation("No server configs found in {0}, not starting any servers.", ServerDefConfigSection);
			}

			Logger.LogInformation("Run command time: {0:0.00} s", (DateTime.UtcNow - StartTime).TotalMilliseconds / 1000);
			Logger.LogInformation("********** RUN MULTISERVER COMMAND COMPLETED **********");

			return ExitCode.Success;
		}

		private FileReference ProjectFullPath;
		public virtual FileReference ProjectPath
		{
			get
			{
				if (ProjectFullPath == null)
				{
					ProjectFullPath = ParseProjectParam();

					if (ProjectFullPath == null)
					{
						throw new AutomationException("No project file specified. Use -project=<project>.");
					}
				}

				return ProjectFullPath;
			}
		}
	}
}
