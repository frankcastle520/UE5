// Copyright Epic Games, Inc. All Rights Reserved.

using System.ComponentModel;
using EpicGames.Core;
using EpicGames.Horde;
using EpicGames.Horde.Storage;
using EpicGames.Horde.Storage.Nodes;
using EpicGames.Horde.Tools;
using Microsoft.Extensions.Logging;

namespace Horde.Commands
{
	[Command("tool", "upload", "Uploads a tool from a local directory")]
	class ToolUpload : Command
	{
		[CommandLine("-Id=")]
		[Description("Identifier for the tool to upload")]
		public ToolId ToolId { get; set; }

		[CommandLine("-Version=")]
		[Description("Optional version number for the new upload")]
		public string? Version { get; set; }

		[CommandLine("-InputDir=")]
		[Description("Directory containing files to upload for the tool")]
		public DirectoryReference InputDir { get; set; } = null!;

		readonly HordeHttpClient _hordeHttpClient;
		readonly HttpStorageClient _storageClient;

		public ToolUpload(HordeHttpClient httpClient, HttpStorageClient storageClient)
		{
			_hordeHttpClient = httpClient;
			_storageClient = storageClient;
		}

		/// <inheritdoc/>
		public override async Task<int> ExecuteAsync(ILogger logger)
		{
			IStorageNamespace storageNamespace = _storageClient.GetNamespaceWithPath($"api/v1/tools/{ToolId}");

			IHashedBlobRef<DirectoryNode> target;
			await using (IBlobWriter writer = storageNamespace.CreateBlobWriter())
			{
				target = await writer.WriteFilesAsync(InputDir);
			}

			ToolDeploymentId deploymentId = await _hordeHttpClient.CreateToolDeploymentAsync(ToolId, Version, null, null, target.GetRefValue());
			logger.LogInformation("Created deployment {DeploymentId}", deploymentId);

			return 0;
		}
	}
}
