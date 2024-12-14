// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;
using System.Xml;
using EpicGames.Core;
using EpicGames.Jupiter;
using Microsoft.Extensions.Logging;

namespace AutomationTool.Tasks
{
	/// <summary>
	/// Parameters for a zip task
	/// </summary>
	public class UploadTaskParameters
	{
		/// <summary>
		/// The directory to read compressed files from.
		/// </summary>
		[TaskParameter]
		public DirectoryReference FromDir { get; set; }

		/// <summary>
		/// List of file specifications separated by semicolons (for example, *.cpp;Engine/.../*.bat), or the name of a tag set. Relative paths are taken from FromDir.
		/// </summary>
		[TaskParameter(Optional = true, ValidationType = TaskParameterValidationType.FileSpec)]
		public string Files { get; set; }

		/// <summary>
		/// The jupiter namespace used to upload the build. Used to control who has access to the build.
		/// </summary>
		[TaskParameter]
		public string JupiterNamespace { get; set; }

		/// <summary>
		/// The key of the build as will be used to download the build again. This has to be globally unique for this particular upload.
		/// </summary>
		[TaskParameter]
		public string JupiterKey { get; set; }

		/// <summary>
		/// The type of archive these files are from, will be added to the metadata
		/// </summary>
		[TaskParameter]
		public string ArchiveType { get; set; }

		/// <summary>
		/// The name of the project this set of files are associated with, will be added to the metadata
		/// </summary>
		[TaskParameter]
		public string ProjectName { get; set; }

		/// <summary>
		/// The source control branch these files were generated from, will be added to the metadata
		/// </summary>
		[TaskParameter]
		public string Branch { get; set; }

		/// <summary>
		/// The source control revision these files were generated from, will be added to the metadata
		/// </summary>
		[TaskParameter]
		public string Changelist { get; set; }

		/// <summary>
		/// Specify the url to the Jupiter instance to upload to
		/// </summary>
		[TaskParameter]
		public string JupiterUrl { get; set; }

		/// <summary>
		/// Semi-colon separated list of '=' separated key value mappings to add to the metadata. E.g. Foo=bar;spam=eggs
		/// </summary>
		[TaskParameter(Optional = true)]
		public string AdditionalMetadata { get; set; }

		/// <summary>
		/// If enabled file content is not kept in memory, results in lower memory usage but increased io as file contents needs to be read multiple times (for hashing as well as during upload)
		/// </summary>
		[TaskParameter(Optional = true)]
		public bool LimitMemoryUsage { get; set; } = true;
	}

	/// <summary>
	/// Uploads a set of files to Jupiter for future retrival
	/// </summary>
	[TaskElement("Upload", typeof(UploadTaskParameters))]
	public class UploadTask : BgTaskImpl
	{
		readonly UploadTaskParameters _parameters;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="parameters">Parameters for this task</param>
		public UploadTask(UploadTaskParameters parameters)
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
			// Find all the input files
			List<FileReference> files;
			if (_parameters.Files == null)
			{
				files = DirectoryReference.EnumerateFiles(_parameters.FromDir, "*", System.IO.SearchOption.AllDirectories).ToList();
			}
			else
			{
				files = ResolveFilespec(_parameters.FromDir, _parameters.Files, tagNameToFileSet).ToList();
			}

			// Create the jupiter tree
			Logger.LogInformation("Uploading {NumFiles} files to {Url}...", files.Count, _parameters.JupiterUrl);

			JupiterFileTree fileTree = new JupiterFileTree(_parameters.FromDir, _parameters.LimitMemoryUsage);
			foreach (FileReference file in files)
			{
				fileTree.AddFile(file);
			}

			Dictionary<string, object> metadata = new Dictionary<string, object>
			{
				{"ArchiveType", _parameters.ArchiveType},
				{"Project", _parameters.ProjectName},
				{"Branch", _parameters.Branch},
				{"Changelist", _parameters.Changelist},
			};

			if (_parameters.AdditionalMetadata != null)
			{
				string[] kv = _parameters.AdditionalMetadata.Split(';');
				foreach (string option in kv)
				{
					int separatorIndex = option.IndexOf('=', StringComparison.Ordinal);
					if (separatorIndex == -1)
					{
						continue;
					}

					string key = option.Substring(0, separatorIndex);
					string value = option.Substring(separatorIndex + 1);

					metadata[key] = value;
				}
			}
			// Upload the tree to Jupiter
			_ = await fileTree.UploadToJupiter(_parameters.JupiterUrl, _parameters.JupiterNamespace, _parameters.JupiterKey, metadata);

			// Debug output of which files mapped to which blobs, can be useful to determine which files are constantly being uploaded
			// Json.Save(FileReference.Combine(AutomationTool.Unreal.RootDirectory, "JupiterUpload.json"), Mapping);
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
			return FindTagNamesFromFilespec(_parameters.Files);
		}

		/// <summary>
		/// Finds the tags which are produced by this task
		/// </summary>
		/// <returns>The tag names which are produced by this task</returns>
		public override IEnumerable<string> FindProducedTagNames()
		{
			return Enumerable.Empty<string>();
		}
	}
}
