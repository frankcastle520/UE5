// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Horde.Commits;
using EpicGames.Horde.Streams;
using HordeServer.Perforce;
using HordeServer.Streams;
using Microsoft.Extensions.Options;

namespace HordeServer.Commits
{
	/// <summary>
	/// Provides commit information for streams
	/// </summary>
	class CommitService : ICommitService
	{
		readonly IPerforceService _perforceService;
		readonly IOptionsMonitor<BuildConfig> _buildConfig;

		/// <summary>
		/// Constructor
		/// </summary>
		public CommitService(IPerforceService perforceService, IOptionsMonitor<BuildConfig> buildConfig)
		{
			_perforceService = perforceService;
			_buildConfig = buildConfig;
		}

		/// <inheritdoc/>
		public ICommitCollection GetCollection(StreamConfig streamConfig) => _perforceService.GetCommits(streamConfig);

		/// <inheritdoc/>
		public async ValueTask<CommitIdWithOrder> GetOrderedAsync(StreamId streamId, CommitId commitId, CancellationToken cancellationToken = default)
		{
			if (!_buildConfig.CurrentValue.TryGetStream(streamId, out StreamConfig? streamConfig))
			{
				throw new StreamNotFoundException(streamId);
			}

			ICommitCollection commitCollection = GetCollection(streamConfig);
			return await commitCollection.GetOrderedAsync(commitId, cancellationToken);
		}
	}
}
