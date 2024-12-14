// Copyright Epic Games, Inc. All Rights Reserved.

using System.Collections.Generic;
using System.Threading.Tasks;
using System.Xml;
using EpicGames.Core;
using UnrealBuildBase;

namespace AutomationTool.Tasks
{
	/// <summary>
	/// Parameters for a DotNet task
	/// </summary>
	public class DotNetTaskParameters
	{
		/// <summary>
		/// Docker command line arguments
		/// </summary>
		[TaskParameter(Optional = true)]
		public string Arguments { get; set; }

		/// <summary>
		/// Base directory for running the command
		/// </summary>
		[TaskParameter(Optional = true)]
		public string BaseDir { get; set; }

		/// <summary>
		/// Environment variables to set
		/// </summary>
		[TaskParameter(Optional = true)]
		public string Environment { get; set; }

		/// <summary>
		/// File to read environment variables from
		/// </summary>
		[TaskParameter(Optional = true)]
		public string EnvironmentFile { get; set; }

		/// <summary>
		/// The minimum exit code, which is treated as an error.
		/// </summary>
		[TaskParameter(Optional = true)]
		public int ErrorLevel { get; set; } = 1;

		/// <summary>
		/// Override path to dotnet executable
		/// </summary>
		[TaskParameter(Optional = true)]
		public FileReference DotNetPath { get; set; }
	}

	/// <summary>
	/// Spawns Docker and waits for it to complete.
	/// </summary>
	[TaskElement("DotNet", typeof(DotNetTaskParameters))]
	public class DotNetTask : SpawnTaskBase
	{
		readonly DotNetTaskParameters _parameters;

		/// <summary>
		/// Construct a Docker task
		/// </summary>
		/// <param name="parameters">Parameters for the task</param>
		public DotNetTask(DotNetTaskParameters parameters)
		{
			_parameters = parameters;
		}

		/// <summary>
		/// ExecuteAsync the task.
		/// </summary>
		/// <param name="job">Information about the current job</param>
		/// <param name="buildProducts">Set of build products produced by this node.</param>
		/// <param name="tagNameToFileSet">Mapping from tag names to the set of files they include</param>
		public override async Task ExecuteAsync(JobContext job, HashSet<FileReference> buildProducts, Dictionary<string, HashSet<FileReference>> tagNameToFileSet)
		{
			FileReference dotNetFile = _parameters.DotNetPath == null ? Unreal.DotnetPath : _parameters.DotNetPath;
			if (!FileReference.Exists(dotNetFile))
			{
				throw new AutomationException("DotNet is missing from {0}", dotNetFile);
			}

			IProcessResult result = await ExecuteAsync(dotNetFile.FullName, _parameters.Arguments, workingDir: _parameters.BaseDir, envVars: ParseEnvVars(_parameters.Environment, _parameters.EnvironmentFile));
			if (result.ExitCode < 0 || result.ExitCode >= _parameters.ErrorLevel)
			{
				throw new AutomationException("Docker terminated with an exit code indicating an error ({0})", result.ExitCode);
			}
		}

		/// <summary>
		/// Output this task out to an XML writer.
		/// </summary>
		public override void Write(XmlWriter writer)
		{
			Write(writer, _parameters);
		}

		/// <summary>
		/// Find all the tags which are used as inputs to this task
		/// </summary>
		/// <returns>The tag names which are read by this task</returns>
		public override IEnumerable<string> FindConsumedTagNames()
		{
			yield break;
		}

		/// <summary>
		/// Find all the tags which are modified by this task
		/// </summary>
		/// <returns>The tag names which are modified by this task</returns>
		public override IEnumerable<string> FindProducedTagNames()
		{
			yield break;
		}
	}

	public static partial class StandardTasks
	{
		/// <summary>
		/// Runs a command using dotnet.
		/// </summary>
		/// <param name="arguments">Command-line arguments.</param>
		/// <param name="baseDir">Base directory for running the command.</param>
		/// <param name="environment">Environment variables to set.</param>
		/// <param name="environmentFile">File to read environment variables from.</param>
		/// <param name="errorLevel">The minimum exit code, which is treated as an error.</param>
		/// <param name="dotNetPath">Override path to dotnet executable.</param>
		public static async Task DotNetAsync(string arguments = null, DirectoryReference baseDir = null, string environment = null, FileReference environmentFile = null, int errorLevel = 1, FileReference dotNetPath = null)
		{
			DotNetTaskParameters parameters = new DotNetTaskParameters();
			parameters.Arguments = arguments;
			parameters.BaseDir = baseDir?.FullName;
			parameters.Environment = environment;
			parameters.EnvironmentFile = environmentFile?.FullName;
			parameters.ErrorLevel = errorLevel;
			parameters.DotNetPath = dotNetPath;

			await ExecuteAsync(new DotNetTask(parameters));
		}
	}
}
