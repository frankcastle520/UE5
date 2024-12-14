// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Horde.Server;
using EpicGames.Perforce;
using HordeServer.Configuration;
using HordeServer.Utilities;
using Microsoft.AspNetCore.Authorization;
using Microsoft.AspNetCore.Mvc;

namespace HordeServer.Perforce
{
	/// <summary>
	/// Implements preflight of config changes with Perforce
	/// </summary>
	[ApiController]
	[Authorize]
	[Route("[controller]")]
	public class ServerPerforceController : HordeControllerBase
	{
		readonly IConfigService _configService;
		readonly IPerforceService _perforceService;

		/// <summary>
		/// Constructor
		/// </summary>
		public ServerPerforceController(IConfigService configService, IPerforceService perforceService)
		{
			_configService = configService;
			_perforceService = perforceService;
		}

		/// <summary>
		/// Returns settings for automating auth against this server
		/// </summary>
		[HttpPost]
		[Route("/api/v1/server/preflightconfig")]
		public async Task<ActionResult<PreflightConfigResponse>> PreflightConfigAsync(PreflightConfigRequest request, CancellationToken cancellationToken)
		{
			string cluster = request.Cluster ?? "default";

			IPooledPerforceConnection perforce = await _perforceService.ConnectAsync(cluster, cancellationToken: cancellationToken);

			PerforceResponse<DescribeRecord> describeResponse = await perforce.TryDescribeAsync(DescribeOptions.Shelved, -1, request.ShelvedChange, cancellationToken);
			if (!describeResponse.Succeeded)
			{
				return BadRequest(KnownLogEvents.Horde_InvalidPreflight, "CL {Change} does not exist.", request.ShelvedChange);
			}

			DescribeRecord record = describeResponse.Data;

			List<string> configFiles = new List<string> { "/globals.json", "global.json", ".project.json", ".stream.json", ".dashboard.json", ".telemetry.json" };

			Dictionary<Uri, byte[]> files = new Dictionary<Uri, byte[]>();
			foreach (DescribeFileRecord fileRecord in record.Files)
			{
				if (configFiles.FirstOrDefault(config => fileRecord.DepotFile.EndsWith(config, StringComparison.OrdinalIgnoreCase)) != null)
				{
					PerforceResponse<PrintRecord<byte[]>> printRecordResponse = await perforce.TryPrintDataAsync($"{fileRecord.DepotFile}@={request.ShelvedChange}", cancellationToken);
					if (!printRecordResponse.Succeeded || printRecordResponse.Data.Contents == null)
					{
						return BadRequest($"Unable to print contents of {fileRecord.DepotFile}@={request.ShelvedChange}");
					}

					PrintRecord<byte[]> printRecord = printRecordResponse.Data;

					Uri uri = new Uri($"perforce://{cluster}{printRecord.DepotFile}");
					files.Add(uri, printRecord.Contents);
				}
			}

			if (files.Count == 0)
			{
				return BadRequest(KnownLogEvents.Horde_InvalidPreflight, "No config files found in CL {Change}.", request.ShelvedChange);
			}

			string? message = await _configService.ValidateAsync(files, cancellationToken);

			PreflightConfigResponse response = new PreflightConfigResponse();
			response.Result = message == null;
			response.Message = message;

			return response;
		}
	}
}
