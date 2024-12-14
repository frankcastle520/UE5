// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using Microsoft.Build.Evaluation;
using Microsoft.Build.Execution;
using Microsoft.Build.Framework;
using Microsoft.Build.Graph;
using Microsoft.Build.Locator;
using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text.Json;

namespace EpicGames.MsBuild
{
	using ILogger = Microsoft.Extensions.Logging.ILogger;
	using IBuildLogger = Microsoft.Build.Framework.ILogger;

	/// <summary>
	/// Builds .csproj files
	/// </summary>
	public static class CsProjBuilder
	{
		class MLogger : IBuildLogger
		{
			readonly ILogger _inner;

			LoggerVerbosity IBuildLogger.Verbosity { get => LoggerVerbosity.Normal; set => throw new NotImplementedException(); }
			string IBuildLogger.Parameters { get => throw new NotImplementedException(); set { } }

			public bool _bVeryVerboseLog = false;

			bool _bFirstError = true;

			public MLogger(ILogger inInner)
			{
				_inner = inInner;
			}

			void IBuildLogger.Initialize(IEventSource eventSource)
			{
				eventSource.ProjectStarted += new ProjectStartedEventHandler(EventSource_ProjectStarted);
				eventSource.TaskStarted += new TaskStartedEventHandler(EventSource_TaskStarted);
				eventSource.MessageRaised += new BuildMessageEventHandler(EventSource_MessageRaised);
				eventSource.WarningRaised += new BuildWarningEventHandler(EventSource_WarningRaised);
				eventSource.ErrorRaised += new BuildErrorEventHandler(EventSource_ErrorRaised);
				eventSource.ProjectFinished += new ProjectFinishedEventHandler(EventSource_ProjectFinished);
			}

			void EventSource_ErrorRaised(object sender, BuildErrorEventArgs e)
			{
				if (_bFirstError)
				{
					Trace.WriteLine("");
					_inner.LogInformation("");
					_bFirstError = false;
				}
				_inner.LogError("{File}({Line},{Column}): error {Code}: {Message} ({ProjectFile})", new FileReference(e.File), new LogValue(LogValueType.LineNumber, e.LineNumber.ToString()), new LogValue(LogValueType.ColumnNumber, e.ColumnNumber.ToString()), new LogValue(LogValueType.ErrorCode, e.Code), e.Message, new FileReference(e.ProjectFile));
			}

			void EventSource_WarningRaised(object sender, BuildWarningEventArgs e)
			{
				{
					// workaround for warnings that appear after revert of net6.0 upgrade. Delete this block when the net6.0 upgrade is done.
					// ...\Engine\Binaries\ThirdParty\DotNet\Windows\sdk\3.1.403\Microsoft.Common.CurrentVersion.targets(3036,5): warning MSB3088: Could not read state file "obj\Development\[projectname].csproj.GenerateResource.cache". The input stream is not a valid binary format.
					// The starting contents (in bytes) are: 06-01-01-00-00-00-01-19-50-72-6F-70-65-72-74-69-65 ... (...\[projectname].csproj)
					if (String.Equals(e.Code, "MSB3088", StringComparison.Ordinal))
					{
						_inner.LogDebug("{File}({Line},{Column}): suppressed warning {Code}: {Message} ({ProjectFile})", new FileReference(e.File), new LogValue(LogValueType.LineNumber, e.LineNumber.ToString()), new LogValue(LogValueType.ColumnNumber, e.ColumnNumber.ToString()), new LogValue(LogValueType.ErrorCode, e.Code), e.Message, new FileReference(e.ProjectFile));
						return;
					}
				}

				if (_bFirstError)
				{
					_inner.LogInformation("");
					_bFirstError = false;
				}

				_inner.LogWarning("{File}({Line},{Column}): warning {Code}: {Message} ({ProjectFile})", new FileReference(e.File), new LogValue(LogValueType.LineNumber, e.LineNumber.ToString()), new LogValue(LogValueType.ColumnNumber, e.ColumnNumber.ToString()), new LogValue(LogValueType.ErrorCode, e.Code), e.Message, new FileReference(e.ProjectFile));
			}

			void EventSource_MessageRaised(object sender, BuildMessageEventArgs e)
			{
				if (_bVeryVerboseLog)
				{
					//if (!String.Equals(e.SenderName, "ResolveAssemblyReference"))
					//if (e.Message.Contains("atic"))
					{
						_inner.LogDebug("{SenderName}: {Message}", e.SenderName, e.Message);
					}
				}
			}

			void EventSource_ProjectStarted(object sender, ProjectStartedEventArgs e)
			{
				if (_bVeryVerboseLog)
				{
					_inner.LogDebug("{SenderName}: {Message}", e.SenderName, e.Message);
				}
			}

			void EventSource_ProjectFinished(object sender, ProjectFinishedEventArgs e)
			{
				if (_bVeryVerboseLog)
				{
					_inner.LogDebug("{SenderName}: {Message}", e.SenderName, e.Message);
				}
			}

			void EventSource_TaskStarted(object sender, TaskStartedEventArgs e)
			{
				if (_bVeryVerboseLog)
				{
					_inner.LogDebug("{SenderName}: {Message}", e.SenderName, e.Message);
				}
			}

			void IBuildLogger.Shutdown()
			{
			}
		}

		static FileReference ConstructBuildRecordPath(CsProjBuildHook hook, FileReference projectPath, IEnumerable<DirectoryReference> baseDirectories)
		{
			DirectoryReference basePath = null;

			foreach (DirectoryReference scriptFolder in baseDirectories)
			{
				if (projectPath.IsUnderDirectory(scriptFolder))
				{
					basePath = scriptFolder;
					break;
				}
			}

			if (basePath == null)
			{
				throw new Exception($"Unable to map csproj {projectPath} to Engine, game, or an additional script folder. Candidates were:{Environment.NewLine} {String.Join(Environment.NewLine, baseDirectories)}");
			}

			DirectoryReference buildRecordDirectory = hook.GetBuildRecordDirectory(basePath);
			DirectoryReference.CreateDirectory(buildRecordDirectory);

			return FileReference.Combine(buildRecordDirectory, projectPath.GetFileName()).ChangeExtension(".json");
		}

		/// <summary>
		/// Builds multiple projects
		/// </summary>
		/// <param name="foundProjects">Collection of project to be built</param>
		/// <param name="bForceCompile">If true, force the compilation of the projects</param>
		/// <param name="bBuildSuccess">Set to true/false depending on if all projects compiled or are up-to-date</param>
		/// <param name="hook">Interface to fetch data about the building environment</param>
		/// <param name="baseDirectories">Base directories of the engine and project</param>
		/// <param name="defineConstants">Collection of constants to be defined while building projects</param>
		/// <param name="onBuildingProjects">Action invoked to notify caller regarding the number of projects being built</param>
		/// <param name="logger">Destination logger</param>
		public static Dictionary<FileReference, CsProjBuildRecordEntry> Build(HashSet<FileReference> foundProjects,
			bool bForceCompile, out bool bBuildSuccess, CsProjBuildHook hook, IEnumerable<DirectoryReference> baseDirectories,
			IEnumerable<string> defineConstants, Action<int> onBuildingProjects, ILogger logger)
		{

			// Register the MS build path prior to invoking the internal routine.  By not having the internal routine
			// inline, we avoid having the issue of the Microsoft.Build libraries being resolved prior to the build path
			// being set.
			RegisterMsBuildPath(hook);
			return BuildInternal(foundProjects, bForceCompile, out bBuildSuccess, hook, baseDirectories, defineConstants, onBuildingProjects, logger);
		}

		/// <summary>
		/// Builds multiple projects.  This is the internal implementation invoked after the MS build path is set
		/// </summary>
		/// <param name="foundProjects">Collection of project to be built</param>
		/// <param name="bForceCompile">If true, force the compilation of the projects</param>
		/// <param name="bBuildSuccess">Set to true/false depending on if all projects compiled or are up-to-date</param>
		/// <param name="hook">Interface to fetch data about the building environment</param>
		/// <param name="baseDirectories">Base directories of the engine and project</param>
		/// <param name="defineConstants">Collection of constants to be defined while building projects</param>
		/// <param name="onBuildingProjects">Action invoked to notify caller regarding the number of projects being built</param>
		/// <param name="logger">Destination logger</param>
		private static Dictionary<FileReference, CsProjBuildRecordEntry> BuildInternal(HashSet<FileReference> foundProjects,
			bool bForceCompile, out bool bBuildSuccess, CsProjBuildHook hook, IEnumerable<DirectoryReference> baseDirectories, IEnumerable<string> defineConstants,
			Action<int> onBuildingProjects, ILogger logger)
		{
			Dictionary<string, string> globalProperties = new Dictionary<string, string>
			{
				{ "EngineDir", hook.EngineDirectory.FullName },
#if DEBUG
				{ "Configuration", "Debug" },
#else
				{ "Configuration", "Development" },
#endif
			};

			if (defineConstants.Any())
			{
				globalProperties.Add("DefineConstants", String.Join(';', defineConstants));
			}

			Dictionary<FileReference, CsProjBuildRecordEntry> buildRecords = new();

			using ProjectCollection projectCollection = new ProjectCollection(globalProperties);
			Dictionary<string, Project> projects = new Dictionary<string, Project>();
			HashSet<string> skippedProjects = new HashSet<string>();

			// Microsoft.Build.Evaluation.Project provides access to information stored in the .csproj xml that is 
			// not available when using Microsoft.Build.Execution.ProjectInstance (used later in this function and
			// in BuildProjects) - particularly, to access glob information defined in the source file.

			// Load all found projects, and any other referenced projects.
			foreach (FileReference projectPath in foundProjects)
			{
				void LoadProjectAndReferences(string projectPath, string referencedBy)
				{
					projectPath = Path.GetFullPath(projectPath);
					if (!projects.ContainsKey(projectPath) && !skippedProjects.Contains(projectPath))
					{
						Project project;

						// Microsoft.Build.Evaluation.Project doesn't give a lot of useful information if this fails,
						// so make sure to print our own diagnostic info if something goes wrong
						try
						{
							project = new Project(projectPath, globalProperties, toolsVersion: null, projectCollection: projectCollection);
						}
						catch (Microsoft.Build.Exceptions.InvalidProjectFileException iPFEx)
						{
							logger.LogError("Could not load project file {ProjectPath}", projectPath);
							logger.LogError("{Message}", iPFEx.BaseMessage);

							if (!String.IsNullOrEmpty(referencedBy))
							{
								logger.LogError("Referenced by: {ReferencedBy}", referencedBy);
							}
							if (projects.Count > 0)
							{
								logger.LogError("See the log file for the list of previously loaded projects.");
								logger.LogError("Loaded projects (most recently loaded first):");
								foreach (string path in projects.Keys.Reverse())
								{
									logger.LogError("  {Path}", path);
								}
							}
							throw;
						}

						if (!OperatingSystem.IsWindows())
						{
							// check the TargetFramework of the project: we can't build Windows-only projects on 
							// non-Windows platforms.
							if (project.GetProperty("TargetFramework").EvaluatedValue.Contains("windows", StringComparison.Ordinal))
							{
								skippedProjects.Add(projectPath);
								logger.LogInformation("Skipping windows-only project {ProjectPath}", projectPath);
								return;
							}
						}

						projects.Add(projectPath, project);
						referencedBy = String.IsNullOrEmpty(referencedBy) ? projectPath : $"{projectPath}{Environment.NewLine}{referencedBy}";
						foreach (string referencedProject in project.GetItems("ProjectReference").
							Select(i => i.EvaluatedInclude))
						{
							LoadProjectAndReferences(Path.Combine(project.DirectoryPath, referencedProject), referencedBy);
						}
					}
				}
				LoadProjectAndReferences(projectPath.FullName, null);
			}

			// generate a BuildRecord for each loaded project - the gathered information will be used to determine if the project is
			// out of date, and if building this project can be skipped. It is also used to populate Intermediate/ScriptModules after the
			// build completes
			foreach (Project project in projects.Values)
			{
				string targetPath = Path.GetRelativePath(project.DirectoryPath, project.GetPropertyValue("TargetPath"));

				FileReference projectPath = FileReference.FromString(project.FullPath);
				FileReference buildRecordPath = ConstructBuildRecordPath(hook, projectPath, baseDirectories);

				CsProjBuildRecord buildRecord = new CsProjBuildRecord()
				{
					Version = CsProjBuildRecord.CurrentVersion,
					TargetPath = targetPath,
					TargetBuildTime = hook.GetLastWriteTime(project.DirectoryPath, targetPath),
					ProjectPath = Path.GetRelativePath(buildRecordPath.Directory.FullName, project.FullPath)
				};

				// the .csproj
				buildRecord.Dependencies.Add(Path.GetRelativePath(project.DirectoryPath, project.FullPath));

				// Imports: files included in the xml (typically props, targets, etc)
				foreach (ResolvedImport import in project.Imports)
				{
					string importPath = Path.GetRelativePath(project.DirectoryPath, import.ImportedProject.FullPath);

					// nuget.g.props and nuget.g.targets are generated by Restore, and are frequently re-written;
					// it should be safe to ignore these files - changes to references from a .csproj file will
					// show up as that file being out of date.
					if (importPath.Contains("nuget.g.", StringComparison.Ordinal))
					{
						continue;
					}

					buildRecord.Dependencies.Add(importPath);
				}

				// References: e.g. Ionic.Zip.Reduced.dll, fastJSON.dll
				foreach (ProjectItem item in project.GetItems("Reference"))
				{
					buildRecord.Dependencies.Add(item.GetMetadataValue("HintPath"));
				}

				foreach (ProjectItem referencedProjectItem in project.GetItems("ProjectReference"))
				{
					buildRecord.ProjectReferencesAndTimes.Add(new CsProjBuildRecordRef { ProjectPath = referencedProjectItem.EvaluatedInclude });
				}

				foreach (ProjectItem compileItem in project.GetItems("Compile"))
				{
					if (hook.HasWildcards(compileItem.UnevaluatedInclude))
					{
						buildRecord.GlobbedDependencies.Add(compileItem.EvaluatedInclude);
					}
					else
					{
						buildRecord.Dependencies.Add(compileItem.EvaluatedInclude);
					}
				}

				foreach (ProjectItem contentItem in project.GetItems("Content"))
				{
					if (hook.HasWildcards(contentItem.UnevaluatedInclude))
					{
						buildRecord.GlobbedDependencies.Add(contentItem.EvaluatedInclude);
					}
					else
					{
						buildRecord.Dependencies.Add(contentItem.EvaluatedInclude);
					}
				}

				foreach (ProjectItem embeddedResourceItem in project.GetItems("EmbeddedResource"))
				{
					if (hook.HasWildcards(embeddedResourceItem.UnevaluatedInclude))
					{
						buildRecord.GlobbedDependencies.Add(embeddedResourceItem.EvaluatedInclude);
					}
					else
					{
						buildRecord.Dependencies.Add(embeddedResourceItem.EvaluatedInclude);
					}
				}

				// this line right here is slow: ~30-40ms per project (which can be more than a second total)
				// making it one of the slowest steps in gathering or checking dependency information from
				// .csproj files (after loading as Microsoft.Build.Evalation.Project)
				// 
				// This also returns a lot more information than we care for - MSBuildGlob objects,
				// which have a range of precomputed values. It may be possible to take source for
				// GetAllGlobs() and construct a version that does less.
				List<GlobResult> globs = project.GetAllGlobs();

				// FileMatcher.IsMatch() requires directory separators in glob strings to match the
				// local flavor. There's probably a better way.
				string CleanGlobString(string globString)
				{
					char sep = Path.DirectorySeparatorChar;
					char notSep = sep == '/' ? '\\' : '/'; // AltDirectorySeparatorChar isn't always what we need (it's '/' on Mac)

					char[] chars = globString.ToCharArray();
					int p = 0;
					for (int i = 0; i < globString.Length; ++i, ++p)
					{
						// Flip a non-native separator
						if (chars[i] == notSep)
						{
							chars[p] = sep;
						}
						else
						{
							chars[p] = chars[i];
						}

						// Collapse adjacent separators
						if (i > 0 && chars[p] == sep && chars[p - 1] == sep)
						{
							p -= 1;
						}
					}

					return new string(chars, 0, p);
				}

				foreach (GlobResult glob in globs)
				{
					if (String.Equals("None", glob.ItemElement.ItemType, StringComparison.Ordinal))
					{
						// don't record the default "None" glob - it's not (?) a trigger for any rebuild
						continue;
					}

					List<string> include = new List<string>(glob.IncludeGlobs.Select(f => CleanGlobString(f))).OrderBy(x => x).ToList();
					List<string> exclude = new List<string>(glob.Excludes.Select(f => CleanGlobString(f))).OrderBy(x => x).ToList();
					List<string> remove = new List<string>(glob.Removes.Select(f => CleanGlobString(f))).OrderBy(x => x).ToList();

					buildRecord.Globs.Add(new CsProjBuildRecord.Glob()
					{
						ItemType = glob.ItemElement.ItemType,
						Include = include,
						Exclude = exclude,
						Remove = remove
					});
				}

				CsProjBuildRecordEntry entry = new CsProjBuildRecordEntry(projectPath, buildRecordPath, buildRecord);
				buildRecords.Add(entry.ProjectFile, entry);
			}

			// Potential optimization: Constructing the ProjectGraph here gives the full graph of dependencies - which is nice,
			// but not strictly necessary, and slower than doing it some other way.
			ProjectGraph inputProjectGraph;
			inputProjectGraph = new ProjectGraph(foundProjects
				// Build the graph without anything that can't be built on this platform
				.Where(x => !skippedProjects.Contains(x.FullName))
				.Select(p => p.FullName), globalProperties, projectCollection);

			// A ProjectGraph that will represent the set of projects that we actually want to build
			ProjectGraph buildProjectGraph = null;

			if (bForceCompile)
			{
				logger.LogDebug("Script modules will build: '-Compile' on command line");
				buildProjectGraph = inputProjectGraph;
			}
			else
			{
				foreach (ProjectGraphNode project in inputProjectGraph.ProjectNodesTopologicallySorted)
				{
					hook.ValidateRecursively(buildRecords, FileReference.FromString(project.ProjectInstance.FullPath));
				}

				// Select the projects that have been found to be out of date
				Dictionary<FileReference, CsProjBuildRecordEntry> invalidBuildRecords = new(buildRecords.Where(x => x.Value.Status == CsProjBuildRecordStatus.Invalid));
				HashSet<ProjectGraphNode> outOfDateProjects = new HashSet<ProjectGraphNode>(inputProjectGraph.ProjectNodes.Where(x => invalidBuildRecords.ContainsKey(FileReference.FromString(x.ProjectInstance.FullPath))));

				if (outOfDateProjects.Count > 0)
				{
					buildProjectGraph = new ProjectGraph(outOfDateProjects.Select(p => p.ProjectInstance.FullPath), globalProperties, projectCollection);
				}
			}

			if (buildProjectGraph != null)
			{
				onBuildingProjects(buildProjectGraph.EntryPointNodes.Count);
				bBuildSuccess = BuildProjects(buildProjectGraph, globalProperties, logger);
			}
			else
			{
				bBuildSuccess = true;
			}

			// Update the target times
			foreach (ProjectGraphNode projectNode in inputProjectGraph.ProjectNodes)
			{
				FileReference projectPath = FileReference.FromString(projectNode.ProjectInstance.FullPath);
				CsProjBuildRecordEntry entry = buildRecords[projectPath];
				FileReference fullPath = FileReference.Combine(projectPath.Directory, entry.BuildRecord.TargetPath);
				entry.BuildRecord.TargetBuildTime = FileReference.GetLastWriteTime(fullPath);
			}

			// Update the project reference target times
			foreach (ProjectGraphNode projectNode in inputProjectGraph.ProjectNodes)
			{
				FileReference projectPath = FileReference.FromString(projectNode.ProjectInstance.FullPath);
				CsProjBuildRecordEntry entry = buildRecords[projectPath];
				foreach (CsProjBuildRecordRef referencedProject in entry.BuildRecord.ProjectReferencesAndTimes)
				{
					FileReference refProjectPath = FileReference.FromString(Path.GetFullPath(referencedProject.ProjectPath, projectPath.Directory.FullName));
					if (buildRecords.TryGetValue(refProjectPath, out CsProjBuildRecordEntry refEntry))
					{
						referencedProject.TargetBuildTime = refEntry.BuildRecord.TargetBuildTime;
					}
				}
			}

			// write all build records
			foreach (ProjectGraphNode projectNode in inputProjectGraph.ProjectNodes)
			{
				FileReference projectPath = FileReference.FromString(projectNode.ProjectInstance.FullPath);
				CsProjBuildRecordEntry entry = buildRecords[projectPath];
				if (FileReference.WriteAllTextIfDifferent(entry.BuildRecordFile,
					JsonSerializer.Serialize(entry.BuildRecord, new JsonSerializerOptions { WriteIndented = true })))
				{
					logger.LogDebug("Wrote script module build record to {BuildRecordPath}", entry.BuildRecordFile);
				}
			}

			// todo: re-verify build records after a build to verify that everything is actually up to date

			// even if only a subset was built, this function returns the full list of target assembly paths
			Dictionary<FileReference, CsProjBuildRecordEntry> outDict = new();
			foreach (ProjectGraphNode entryPointNode in inputProjectGraph.EntryPointNodes)
			{
				FileReference projectPath = FileReference.FromString(entryPointNode.ProjectInstance.FullPath);
				outDict.Add(projectPath, buildRecords[projectPath]);
			}
			return outDict;
		}

		private static bool BuildProjects(ProjectGraph projectGraph, Dictionary<string, string> globalProperties, ILogger logger)
		{
			DateTime startTime = DateTime.UtcNow;
			MLogger buildLogger = new MLogger(logger);

			string[] targetsToBuild = { "Restore", "Build" };

			bool result = true;

			foreach (string targetToBuild in targetsToBuild)
			{
				GraphBuildRequestData graphRequest = new GraphBuildRequestData(projectGraph, new string[] { targetToBuild });

				BuildManager buildMan = BuildManager.DefaultBuildManager;

				BuildParameters buildParameters = new BuildParameters();
				buildParameters.AllowFailureWithoutError = false;
				buildParameters.DetailedSummary = true;

				buildParameters.Loggers = new List<IBuildLogger> { buildLogger };
				buildParameters.MaxNodeCount = 1; // msbuild bug - more than 1 here and the build stalls. Likely related to https://github.com/dotnet/msbuild/issues/1941

				buildParameters.OnlyLogCriticalEvents = false;
				buildParameters.ShutdownInProcNodeOnBuildFinish = false;

				buildParameters.GlobalProperties = globalProperties;

				logger.LogInformation(" {TargetToBuild}...", targetToBuild);

				GraphBuildResult buildResult = buildMan.Build(buildParameters, graphRequest);

				if (buildResult.OverallResult == BuildResultCode.Failure)
				{
					logger.LogInformation("");
					foreach (KeyValuePair<ProjectGraphNode, BuildResult> nodeResult in buildResult.ResultsByNode)
					{
						if (nodeResult.Value.OverallResult == BuildResultCode.Failure)
						{
							logger.LogError("  Failed to build: {ProjectPath}", new FileReference(nodeResult.Key.ProjectInstance.FullPath));
						}
					}
					result = false;
				}
			}
			logger.LogInformation("Build projects time: {TimeSeconds:0.00} s", (DateTime.UtcNow - startTime).TotalMilliseconds / 1000);

			return result;
		}

		static bool s_hasRegiteredMsBuildPath = false;

		/// <summary>
		/// Register our bundled dotnet installation to be used by Microsoft.Build
		/// This needs to happen in a function called before the first use of any Microsoft.Build types
		/// </summary>
		public static void RegisterMsBuildPath(CsProjBuildHook hook)
		{
			if (s_hasRegiteredMsBuildPath)
			{
				return;
			}
			s_hasRegiteredMsBuildPath = true;

			// Find our bundled dotnet SDK
			List<string> listOfSdks = new List<string>();
			ProcessStartInfo startInfo = new ProcessStartInfo
			{
				FileName = hook.DotnetPath.FullName,
				RedirectStandardOutput = true,
				UseShellExecute = false,
				ArgumentList = { "--list-sdks" }
			};
			startInfo.EnvironmentVariables["DOTNET_MULTILEVEL_LOOKUP"] = "0"; // use only the bundled dotnet installation - ignore any other/system dotnet install

			Process dotnetProcess = Process.Start(startInfo);
			{
				string line;
				while ((line = dotnetProcess.StandardOutput.ReadLine()) != null)
				{
					listOfSdks.Add(line);
				}
			}
			dotnetProcess.WaitForExit();

			if (listOfSdks.Count != 1)
			{
				throw new Exception("Expected only one sdk installed for bundled dotnet");
			}

			// Expected output has this form:
			// 3.1.403 [D:\UE5_Main\engine\binaries\ThirdParty\DotNet\Windows\sdk]
			string sdkVersion = listOfSdks[0].Split(' ')[0];

			DirectoryReference dotnetSdkDirectory = DirectoryReference.Combine(hook.DotnetDirectory, "sdk", sdkVersion);
			if (!DirectoryReference.Exists(dotnetSdkDirectory))
			{
				throw new Exception("Failed to find .NET SDK directory: " + dotnetSdkDirectory.FullName);
			}

			MSBuildLocator.RegisterMSBuildPath(dotnetSdkDirectory.FullName);
		}
	}
}
