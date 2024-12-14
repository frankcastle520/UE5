// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Horde.Storage;

namespace HordeServer.Ddc
{
	class ContentIdStore : IContentIdStore
	{
		readonly IStorageClient _storageClient;

		public ContentIdStore(IStorageClient storageClient)
		{
			_storageClient = storageClient;
		}

		static string GetAlias(BlobId blobId) => BlobService.GetAlias(blobId);
		static string GetAlias(ContentId contentId) => $"cid:{contentId}";

		public async Task<BlobId[]?> ResolveAsync(NamespaceId ns, ContentId contentId, bool mustBeContentId = false, CancellationToken cancellationToken = default)
		{
			IStorageNamespace storageNamespace = _storageClient.GetNamespace(ns);

			BlobAlias? blobAlias = await storageNamespace.FindAliasAsync(GetAlias(contentId), cancellationToken);
			if (blobAlias == null && !mustBeContentId)
			{
				blobAlias = await storageNamespace.FindAliasAsync(GetAlias(contentId.AsBlobIdentifier()), cancellationToken);
			}
			if (blobAlias == null)
			{
				return null;
			}

			IoHash hash = new IoHash(blobAlias.Data.Span);
			return new[] { BlobId.FromIoHash(hash) };
		}

		public async Task PutAsync(NamespaceId ns, ContentId contentId, BlobId blobId, int contentWeight, CancellationToken cancellationToken)
		{
			IStorageNamespace storageNamespace = _storageClient.GetNamespace(ns);

			BlobAlias? blobAlias = await storageNamespace.FindAliasAsync(GetAlias(blobId), cancellationToken);
			if (blobAlias == null)
			{
				throw new BlobNotFoundException(ns, blobId);
			}

			await storageNamespace.AddAliasAsync(GetAlias(contentId), blobAlias.Target, -contentWeight, blobAlias.Data, cancellationToken: cancellationToken);
		}
	}
}
