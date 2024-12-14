// Copyright Epic Games, Inc. All Rights Reserved.

using System.Collections.Generic;
using System.Linq;
using Gauntlet;
using System.IO;
using System;
using static Gauntlet.UnrealSessionInstance;
using AutomationTool;
using UnrealBuildTool;

namespace UE
{
	/// <summary>
	/// Runs cook on the fly 2 test
	/// </summary>
	public class CookOnTheFly : UnrealTestNode<UnrealTestConfiguration>
	{
		string CookedProjectDirectoryPath;
		string CookedSettingsFilePath;
		DateTime StartTime = DateTime.Now;
		bool DeferredClientStarted = false;
		bool ServerStarted = false;
		bool ClientConnected = false;
		bool CookingRequest = false;
		bool CookOnTheFlyModeChange = false;
		bool CookingProcessStarted = false;
		IEnumerable<string> LogCategories = new string[] { "CookOnTheFly", "Cook" };
		public override IEnumerable<string> GetHeartbeatLogCategories() => LogCategories;
		UnrealLogStreamParser EditorLogParser = null;
		UnrealLogStreamParser ClientLogParser = null;
		Checker ClientCookOnTheFlyCheckers = null;

		/// <summary>
		/// Object that allow to execute several callbacks until they return true.
		/// </summary>
		internal class Checker
		{
			private Dictionary<string, Func<bool>> ToValidate;

			public Checker()
			{
				ToValidate = new();
			}

			/// <summary>
			/// Register a callback to validate
			/// </summary>
			/// <param name="ToFound"></param>
			/// <param name="Action"></param>
			public void Validate(string ToFound, Func<bool> Action)
			{
				ToValidate.Add(ToFound, Action);
			}

			/// <summary>
			/// Check all callbacks, return true if all passed.
			/// </summary>
			/// <returns></returns>
			public bool CheckAll()
			{
				bool Aggregate = true;
				foreach(var Item in ToValidate.ToArray())
				{
					if (Item.Value())
					{
						Log.Info($"Validated: {Item.Key}");
						ToValidate.Remove(Item.Key);
					}
					else
					{
						Aggregate = false;
					}
				}

				return Aggregate;
			}
		}

		public CookOnTheFly(Gauntlet.UnrealTestContext InContext)
			: base(InContext)
		{
		}

		protected virtual string GetStartedCookServerString()
		{
			return "Unreal Network File Server is ready";
		}

		protected virtual string GetClientConnectedString()
		{
			return "Client connected";
		}

		protected virtual string GetCookingRequestString()
		{
			return "Received cook request";
		}

		protected virtual string GetCookingProcessString()
		{
			return "Cooked packages";
		}

		protected virtual string GetGameStartString()
		{
			return "Starting Game";
		}

		protected virtual string GetCookModeString()
		{
			return "CookMode=CookOnTheFly";
		}

		protected virtual string GetEngineInitializedString()
		{
			return "Engine is initialized";
		}

		protected virtual string GetReceivedPackagesCookedString()
		{
			return "Received 'PackagesCooked' message";
		}

		public override UnrealTestConfiguration GetConfiguration()
		{
			UnrealTestConfiguration Config = base.GetConfiguration();
			UnrealTestRole EditorRole = Config.RequireRole(UnrealTargetRole.Editor);
			EditorRole.CommandLine += "-run=cook -cookonthefly -zenstore -log";

			UnrealTargetPlatform TargetPlatform = Context.GetRoleContext(Config.GetMainRequiredRole().Type).Platform;
			CookedProjectDirectoryPath = Path.GetDirectoryName(Context.Options.ProjectPath.ToNormalizedPath());
			string ProjectName = Context.Options.Project;
			if (TargetPlatform == UnrealTargetPlatform.Win64)
			{
				CookedSettingsFilePath = CookedProjectDirectoryPath + string.Format("\\Saved\\Cooked\\Windows\\{0}\\Metadata\\CookedSettings.txt", ProjectName);
			}
			else
			{
				CookedSettingsFilePath = CookedProjectDirectoryPath + string.Format("\\Saved\\Cooked\\{0}\\{1}\\Metadata\\CookedSettings.txt", TargetPlatform.ToString(), ProjectName);
			}

			UnrealTestRole ClientRole = Config.RequireRole(UnrealTargetRole.Client);
			string HostIP = UnrealHelpers.GetHostIpAddress();
			if (HostIP == null)
			{
				throw new AutomationException("Could not find local IP address");
			}

			ClientRole.DeferredLaunch = true;
			ClientRole.CommandLine += string.Format("-cookonthefly -filehostip=\"{0}\" -log -LogCmds=\"LogCookOnTheFly Verbose\"", HostIP);

			return Config;
		}

		public override bool StartTest(int Pass, int InNumPasses)
		{
			if (!base.StartTest(Pass, InNumPasses))
			{
				return false;
			}

			StartTime = DateTime.Now;

			return true;
		}

		private bool AttachToEditorLog()
		{
			if (TestInstance.EditorApp != null)
			{
				EditorLogParser = new(TestInstance.EditorApp.GetLogBufferReader());
				return true;
			}

			return false;
		}

		private bool AttachToClientLog()
		{
			if (TestInstance.ClientApps.Any())
			{
				ClientLogParser = new(TestInstance.ClientApps.First().GetLogBufferReader());
				ClientCookOnTheFlyCheckers = new();
				ClientCookOnTheFlyCheckers.Validate(
					"Client Engine initialization", new(() => ClientLogParser.GetLogLinesContaining(GetEngineInitializedString()).Any())
				);
				ClientCookOnTheFlyCheckers.Validate(
					"Client Game started", new(() => ClientLogParser.GetLogLinesContaining(GetGameStartString()).Any())
				);
				ClientCookOnTheFlyCheckers.Validate(
					"Client Received cooked packages", new(() => ClientLogParser.GetLogLinesContaining(GetReceivedPackagesCookedString()).Any())
				);

				return true;
			}

			// no client started yet.
			return false;
		}

		public override void TickTest()
		{
			base.TickTest();

			if (EditorLogParser == null && !AttachToEditorLog())
			{
				return;
			}

			const int TimeoutDuration = 5;
			if ((DateTime.Now - StartTime).TotalMinutes > TimeoutDuration)
			{
				Log.Error("No logfile activity observed in last {0} minutes. Ending test", TimeoutDuration);
				MarkTestComplete();
				SetUnrealTestResult(Gauntlet.TestResult.TimedOut);
			}

			IAppInstance EditorInstance = TestInstance.EditorApp;
			if (EditorInstance == null)
			{
				Log.Error("Editor instance shouldn't be null");
				MarkTestComplete();
				SetUnrealTestResult(Gauntlet.TestResult.Failed);
			}

			EditorLogParser.ReadStream();
			if (!ServerStarted)
			{
				string CompletionString = GetStartedCookServerString();
				if (EditorLogParser.GetLogLinesContaining(CompletionString).Any())
				{
					Log.Info("Found '{0}'. Cook Server Started", CompletionString);
					ServerStarted = true;
				}
			}

			if (ServerStarted && !DeferredClientStarted)
			{
				UnrealSessionInstance SessionInstance = TestSessions.GetValueOrDefault(Name).SessionInstance;
				if (SessionInstance != null)
				{
					IEnumerable<RoleInstance> DeferredRoles = SessionInstance.DeferredRoles;

					if (DeferredRoles.Count().Equals(1))
					{
						try
						{
							SessionInstance.LaunchDeferredRole(DeferredRoles.First().Role);
						}
						catch
						{
							throw new AutomationException("Couldn't launch client");
						}
					}
					else
					{
							Log.Error("There should only be one deferred instance");
							MarkTestComplete();
							SetUnrealTestResult(Gauntlet.TestResult.Failed);
					}
					DeferredClientStarted = true;
				}
			}

			if (!ClientConnected)
			{
				string ClientConnectedString = GetClientConnectedString();

				if (EditorLogParser.GetLogLinesContaining(ClientConnectedString).Any())
				{
					Log.Info("Found '{0}'. Client connected", ClientConnectedString);
					ClientConnected = true;
				}
			}

			if (!CookingRequest)
			{
				string CookingRequestString = GetCookingRequestString();

				if (EditorLogParser.GetLogLinesContaining(CookingRequestString).Any())
				{
					Log.Info("Found '{0}'. Cooking request exists", CookingRequestString);
					CookingRequest = true;
				}
			}

			if (!CookingProcessStarted)
			{
				CookingProcessStarted = EditorLogParser.GetLogLinesContaining(GetCookingProcessString()).Any();
			}

			if (CookingProcessStarted && !CookOnTheFlyModeChange && File.Exists(CookedSettingsFilePath))
			{
				string CookedSettingsFileText = File.ReadAllText(CookedSettingsFilePath);
				string CookModeString = GetCookModeString();
				DateTime CookedDirectoryLastModifiedTime = File.GetLastWriteTime(CookedProjectDirectoryPath + "\\Saved\\Cooked\\");
				if (CookedSettingsFileText.Contains(CookModeString) && (CookedDirectoryLastModifiedTime>StartTime))
				{
					Log.Info("Found '{0}'. The cook mode changed for the project", CookModeString);
					CookOnTheFlyModeChange = true;
				}
			}

			if (CookOnTheFlyModeChange)
			{
				IAppInstance[] ClientInstances = TestInstance.ClientApps;
				IAppInstance ClientInstance = null;
				if (ClientInstances.Count().Equals(1))
				{
					ClientInstance = ClientInstances.First();
				}
				else
				{
					Log.Error("There should only be one client instance");
					MarkTestComplete();
					SetUnrealTestResult(Gauntlet.TestResult.Failed);
				}

				if (ClientInstance == null)
				{
					Log.Error("Client instance shouldn't be null");
					MarkTestComplete();
					SetUnrealTestResult(Gauntlet.TestResult.Failed);
				}

				if (ClientLogParser == null && !AttachToClientLog())
				{
					return;
				}

				ClientLogParser.ReadStream();
				if (ClientCookOnTheFlyCheckers.CheckAll() && CookingProcessStarted)
				{
					Log.Info("Found '{0}', '{1}', '{2}'. The CookOnTheFly log channel is active. The cooking process is taking place.", GetGameStartString(), GetCookingProcessString(), GetReceivedPackagesCookedString());
					MarkTestComplete();
					SetUnrealTestResult(Gauntlet.TestResult.Passed);
				}
			}
		}

		protected override UnrealProcessResult GetExitCodeAndReason(StopReason InReason, UnrealLog InLog, UnrealRoleArtifacts InArtifacts, out string ExitReason, out int ExitCode)
		{
			if (InArtifacts.SessionRole.RoleType == UnrealTargetRole.Editor)
			{
				InLog.EngineInitializedPattern = GetStartedCookServerString();
			}
			return base.GetExitCodeAndReason(InReason, InLog, InArtifacts, out ExitReason, out ExitCode);
		}
	}
}