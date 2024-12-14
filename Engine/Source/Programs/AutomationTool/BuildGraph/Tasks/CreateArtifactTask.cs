// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using System.Xml;
using EpicGames.Core;
using EpicGames.Horde;
using EpicGames.Horde.Artifacts;
using EpicGames.Horde.Commits;
using EpicGames.Horde.Storage;
using EpicGames.Horde.Storage.Nodes;
using EpicGames.Horde.Streams;
using EpicGames.Horde.Symbols;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;

#nullable enable

namespace AutomationTool.Tasks
{
	/// <summary>
	/// Parameters for a <see cref="CreateArtifactTask"/>.
	/// </summary>
	public class CreateArtifactTaskParameters
	{
		/// <summary>
		/// Name of the artifact
		/// </summary>
		[TaskParameter]
		public string Name { get; set; } = null!;

		/// <summary>
		/// The artifact type. Determines the permissions and expiration policy for the artifact.
		/// </summary>
		[TaskParameter]
		public string Type { get; set; } = null!;

		/// <summary>
		/// Description for the artifact. Will be shown through the Horde dashboard.
		/// </summary>
		[TaskParameter(Optional = true)]
		public string? Description { get; set; }

		/// <summary>
		/// Base path for the uploaded files. All the tagged files must be under this directory. Defaults to the workspace root directory.
		/// </summary>
		[TaskParameter(Optional = true)]
		public string? BaseDir { get; set; }

		/// <summary>
		/// Stream containing the artifact.
		/// </summary>
		[TaskParameter(Optional = true)]
		public string? StreamId { get; set; }

		/// <summary>
		/// Commit for the uploaded artifact.
		/// </summary>
		[TaskParameter(Optional = true)]
		public string? Commit { get; set; }

		/// <summary>
		/// Files to include in the artifact.
		/// </summary>
		[TaskParameter(ValidationType = TaskParameterValidationType.FileSpec)]
		public string Files { get; set; } = null!;

		/// <summary>
		/// Queryable keys for this artifact, separated by semicolons.
		/// </summary>
		[TaskParameter(Optional = true)]
		public string? Keys { get; set; }

		/// <summary>
		/// Other metadata for the artifact, separated by semicolons.
		/// </summary>
		[TaskParameter(Optional = true)]
		public string? Metadata { get; set; }

		/// <summary>
		/// Whether to add aliases for symbol files
		/// </summary>
		[TaskParameter(Optional = true)]
		public bool Symbols { get; set; }
	}

	/// <summary>
	/// Uploads an artifact to Horde
	/// </summary>
	[TaskElement("CreateArtifact", typeof(CreateArtifactTaskParameters))]
	public class CreateArtifactTask : BgTaskImpl
	{
		readonly CreateArtifactTaskParameters _parameters;

		/// <summary>
		/// Constructor.
		/// </summary>
		/// <param name="parameters">Parameters for this task.</param>
		public CreateArtifactTask(CreateArtifactTaskParameters parameters)
			=> _parameters = parameters;

		/// <summary>
		/// ExecuteAsync the task.
		/// </summary>
		/// <param name="job">Information about the current job.</param>
		/// <param name="buildProducts">Set of build products produced by this node.</param>
		/// <param name="tagNameToFileSet">Mapping from tag names to the set of files they include.</param>
		public override async Task ExecuteAsync(JobContext job, HashSet<FileReference> buildProducts, Dictionary<string, HashSet<FileReference>> tagNameToFileSet)
		{
			ArtifactName name = new ArtifactName(_parameters.Name);
			ArtifactType type = new ArtifactType(_parameters.Type);
			List<string> keys = (_parameters.Keys ?? string.Empty).Split(';', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries).ToList();
			List<string> metadata = (_parameters.Metadata ?? string.Empty).Split(';', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries).ToList();

			// Add keys for the job that's executing
			string? jobId = Environment.GetEnvironmentVariable("UE_HORDE_JOBID");
			if (!String.IsNullOrEmpty(jobId))
			{
				keys.Add($"job:{jobId}");

				string? stepId = Environment.GetEnvironmentVariable("UE_HORDE_STEPID");
				if (!String.IsNullOrEmpty(stepId))
				{
					keys.Add($"job:{jobId}/step:{stepId}");
				}
			}

			// Figure out the current change and stream id
			StreamId streamId;
			if (!String.IsNullOrEmpty(_parameters.StreamId))
			{
				streamId = new StreamId(_parameters.StreamId);
			}
			else
			{
				string? streamIdEnvVar = Environment.GetEnvironmentVariable("UE_HORDE_STREAMID");
				if (!String.IsNullOrEmpty(streamIdEnvVar))
				{
					streamId = new StreamId(streamIdEnvVar);
				}
				else
				{
					throw new AutomationException("Missing UE_HORDE_STREAMID environment variable; unable to determine current stream.");
				}
			}

			CommitId commitId;
			if (!String.IsNullOrEmpty(_parameters.Commit))
			{
				commitId = new CommitId(_parameters.Commit);
			}
			else
			{
				int change = CommandUtils.P4Env.Changelist;
				if (change > 0)
				{
					commitId = CommitId.FromPerforceChange(CommandUtils.P4Env.Changelist);
				}
				else
				{
					throw new AutomationException("Unknown changelist. Please run with -P4.");
				}
			}

			// Resolve the files to include
			DirectoryReference baseDir = ResolveDirectory(_parameters.BaseDir);
			List<FileReference> files = BgTaskImpl.ResolveFilespec(baseDir, _parameters.Files, tagNameToFileSet).ToList();

			bool validFiles = true;
			foreach (FileReference file in files)
			{
				if (!file.IsUnderDirectory(baseDir))
				{
					Logger.LogError("Artifact file {File} is not under {BaseDir}", file, baseDir);
					validFiles = false;
				}
			}

			if (!validFiles)
			{
				throw new AutomationException($"Unable to create artifact {name} with given file list.");
			}

			// Create the new artifact
			IHordeClient hordeClient = CommandUtils.ServiceProvider.GetRequiredService<IHordeClient>();
			IArtifactBuilder artifact = await hordeClient.Artifacts.CreateAsync(name, type, _parameters.Description, streamId, commitId, keys, metadata);
			Logger.LogInformation("Creating artifact {ArtifactId} '{ArtifactName}' ({ArtifactType}) with namespace {NamespaceId}, ref {RefName} ({Link})", artifact.Id, name, type, artifact.NamespaceId, artifact.RefName, $"{hordeClient.ServerUrl}/api/v1/storage/{artifact.NamespaceId}/refs/{artifact.RefName}");

			// Upload the files
			Stopwatch timer = Stopwatch.StartNew();

			IHashedBlobRef<DirectoryNode> rootRef;
			await using (IBlobWriter blobWriter = artifact.CreateBlobWriter())
			{
				rootRef = await blobWriter.WriteFilesAsync(baseDir, files);
			}

			// Tag any uploaded symbols
			if (_parameters.Symbols)
			{
				foreach (FileReference file in files)
				{
					string? hash = await SymStore.GetHashAsync(file, Logger, default);
					if (hash != null)
					{
						string path = file.MakeRelativeTo(baseDir);

						FileEntry? fileEntry = await FindFileAsync(rootRef, path);
						if (fileEntry == null)
						{
							Logger.LogWarning("Unable to find file {Path} in uploaded data.", path);
						}
						else
						{
							string fileName = file.GetFileName().ToUpperInvariant();
							string alias = $"sym:{fileName}/{hash}/{fileName}";
							Logger.LogInformation("Adding symbol alias: {Alias}", alias);
							await artifact.AddAliasAsync(alias, fileEntry.Target.Handle);
						}
					}
				}
			}

			// Finalize the artifact
			await artifact.CompleteAsync(rootRef);
			Logger.LogInformation("Uploaded artifact {ArtifactId} in {Time:n1}s", artifact.Id, timer.Elapsed.TotalSeconds);
		}

		static async Task<FileEntry?> FindFileAsync(IBlobRef<DirectoryNode> rootDir, string path, CancellationToken cancellationToken = default)
		{
			string[] fragments = path.Split('/', '\\');
			if (fragments.Length == 0)
			{
				return null;
			}

			IBlobRef<DirectoryNode> directoryRef = rootDir;
			for (int idx = 0;; idx++)
			{
				DirectoryNode directory = await directoryRef.ReadBlobAsync(cancellationToken);
				if (idx + 1 < fragments.Length)
				{
					if (directory.TryGetDirectoryEntry(fragments[idx], out DirectoryEntry? directoryEntry))
					{
						directoryRef = directoryEntry.Handle;
					}
					else
					{
						return null;
					}
				}
				else
				{
					if (directory.TryGetFileEntry(fragments[idx], out FileEntry? fileEntry))
					{
						return fileEntry;
					}
					else
					{
						return null;
					}
				}
			}
		}

		/// <inheritdoc/>
		public override void Write(XmlWriter writer)
			=> Write(writer, _parameters);

		/// <inheritdoc/>
		public override IEnumerable<string> FindConsumedTagNames()
			=> FindTagNamesFromFilespec(_parameters.Files);

		/// <inheritdoc/>
		public override IEnumerable<string> FindProducedTagNames()
			=> Enumerable.Empty<string>();
	}
}
