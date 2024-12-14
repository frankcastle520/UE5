// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.Linq;
using System.Threading;
using System.Threading.Channels;
using System.Threading.Tasks;
using EpicGames.Core;
using EpicGames.Horde.Storage.Bundles;
using EpicGames.Horde.Storage.Bundles.V2;

namespace EpicGames.Horde.Storage
{
	/// <summary>
	/// Request for a blob to be read
	/// </summary>
	/// <typeparam name="TContext">User defined context type for the request</typeparam>
	/// <param name="BlobRef">Reference to the blob data</param>
	/// <param name="Context">User defined context</param>
	public record class BatchReadRequest<TContext>(IBlobRef BlobRef, TContext Context);

	/// <summary>
	/// Individual response for a blob that was read
	/// </summary>
	/// <typeparam name="TContext">User defined context type for the request</typeparam>
	/// <param name="BlobData">Data for the blob</param>
	/// <param name="Context">User defined context</param>
	public sealed record class BatchReadResponseItem<TContext>(BlobData BlobData, TContext Context) : IDisposable
	{
		/// <inheritdoc/>
		public void Dispose()
			=> BlobData.Dispose();
	}

	/// <summary>
	/// Response containing a batch of items that were read
	/// </summary>
	/// <typeparam name="TContext">User defined context type</typeparam>
	/// <param name="Items">Items for this response</param>
	public sealed record class BatchReadResponse<TContext>(BatchReadResponseItem<TContext>[] Items) : IDisposable
	{
		/// <inheritdoc/>
		public void Dispose()
		{
			foreach (BatchReadResponseItem<TContext> item in Items)
			{
				item.Dispose();
			}
		}
	}

	/// <summary>
	/// Stats for a batch read
	/// </summary>
	public record class BatchReaderStats(int NumRequests, int NumReads, int NumBundles, int NumPackets, long BytesRead, long BytesDecoded);

	/// <summary>
	/// Implements an efficient pipeline for streaming blob data
	/// </summary>
	/// <typeparam name="TContext">User-defined context type</typeparam>
	public sealed class BatchReader<TContext> : IDisposable
	{
		record class BundleRequest(BundleHandle Handle, int MinOffset, int MaxOffset, List<PacketRequest> Packets);

		record class BundleResponse(IReadOnlyMemoryOwner<byte> Data, int MinOffset, int MaxOffset, List<PacketRequest> Packets) : IDisposable
		{
			public void Dispose()
				=> Data.Dispose();
		}

		record class PacketRequest(FlushedPacketHandle Handle, List<ExportRequest> Exports)
		{
			public int MinOffset => Handle.PacketOffset;
			public int MaxOffset => Handle.PacketOffset + Handle.PacketLength;
		}

		record class ExportRequest(ExportHandle Handle, TContext Context);

		record class PacketResponse(IReadOnlyMemoryOwner<byte> Data, List<ExportRequest> Exports) : IDisposable
		{
			public void Dispose()
				=> Data.Dispose();
		}

		// Number of items to read from the input queue before partitioning into batches
		const int MinQueueLength = 2000;

		// Maximum gap between reads that should be coalesced
		const long CoalesceReadsBelowSize = 2 * 1024 * 1024;

		readonly Channel<BatchReadRequest<TContext>> _blobRequests;
		readonly Channel<BatchReadRequest<TContext>> _otherRequests;
		readonly Channel<BundleRequest> _bundleRequests;

		readonly Channel<BundleResponse> _bundleResponses;
		readonly Channel<PacketResponse> _packetResponses;
		readonly Channel<BatchReadResponse<TContext>> _blobResponses;

		int _numRequests;
		int _numReads;
		int _numBundles;
		int _numPackets;
		long _bytesRead;
		long _bytesDecoded;

		/// <summary>
		/// Access to the request channel
		/// </summary>
		public ChannelWriter<BatchReadRequest<TContext>> RequestWriter
			=> _blobRequests.Writer;

		/// <summary>
		/// Access to the response channel
		/// </summary>
		public ChannelReader<BatchReadResponse<TContext>> ResponseReader
			=> _blobResponses.Reader;

		/// <summary>
		/// Constructor
		/// </summary>
		public BatchReader()
		{
			_blobRequests = Channel.CreateUnbounded<BatchReadRequest<TContext>>();
			_otherRequests = Channel.CreateUnbounded<BatchReadRequest<TContext>>();
			_bundleRequests = Channel.CreateUnbounded<BundleRequest>();
			_bundleResponses = Channel.CreateUnbounded<BundleResponse>();
			_packetResponses = Channel.CreateUnbounded<PacketResponse>();
			_blobResponses = Channel.CreateUnbounded<BatchReadResponse<TContext>>();
		}

		/// <inheritdoc/>
		public void Dispose()
		{
			while (_bundleResponses.Reader.TryRead(out BundleResponse? item))
			{
				item.Dispose();
			}

			while (_packetResponses.Reader.TryRead(out PacketResponse? item))
			{
				item.Dispose();
			}

			while (_blobResponses.Reader.TryRead(out BatchReadResponse<TContext>? item))
			{
				item.Dispose();
			}
		}

		/// <summary>
		/// Gets stats for the reader
		/// </summary>
		public BatchReaderStats GetStats()
			=> new BatchReaderStats(_numRequests, _numReads, _numBundles, _numPackets, _bytesRead, _bytesDecoded);

		/// <summary>
		/// Adds a batch reader to the given pipeline
		/// </summary>
		internal void AddToPipeline(AsyncPipeline pipeline, int numReadTasks, int numDecodeTasks)
		{
			_ = pipeline.AddTask(CreateBundleRequestsAsync);
			Task otherTask = pipeline.AddTask(HandleOtherRequestsAsync);

			// Read bundles and mark the bundle response channel as complete once we're finished
			Task[] bundleTasks = pipeline.AddTasks(numReadTasks, ReadBundlesAsync);
			_ = Task.WhenAll(bundleTasks).ContinueWith(_ => _bundleResponses.Writer.TryComplete(), TaskScheduler.Default);

			// Read packets
			Task[] packetTasks = pipeline.AddTasks(numDecodeTasks, ReadPacketsAsync);

			// Read all the output blobs and mark the blob response channel as complete
			IEnumerable<Task> blobTasks = packetTasks.Append(otherTask);
			_ = Task.WhenAll(blobTasks).ContinueWith(_ => _blobResponses.Writer.TryComplete(), TaskScheduler.Default);
		}

		// Read a blob using the naive read pipeline
		async Task HandleOtherRequestsAsync(CancellationToken cancellationToken)
		{
			await foreach (BatchReadRequest<TContext> request in _otherRequests.Reader.ReadAllAsync(cancellationToken))
			{
				BlobData? blobData = null;
				try
				{
					blobData = await request.BlobRef.ReadBlobDataAsync(cancellationToken);
#pragma warning disable CA2000
					BatchReadResponse<TContext> response = new BatchReadResponse<TContext>([new BatchReadResponseItem<TContext>(blobData, request.Context)]);
					await _blobResponses.Writer.WriteAsync(response, cancellationToken);
#pragma warning restore CA2000
				}
				catch
				{
					blobData?.Dispose();
				}
			}
		}

		#region Grouping requests

		record class PacketExport(FlushedPacketHandle PacketHandle, ExportHandle ExportHandle, TContext Context);

		async Task CreateBundleRequestsAsync(CancellationToken cancellationToken)
		{
			int queueLength = 0;
			Queue<BundleHandle> bundleQueue = new Queue<BundleHandle>();
			Dictionary<BundleHandle, List<PacketExport>> bundleHandleToExportBatch = new Dictionary<BundleHandle, List<PacketExport>>();

			for (; ; )
			{
				// Fill the queue up to the max length
				for (; ; )
				{
					BatchReadRequest<TContext>? request;
					if (!_blobRequests.Reader.TryRead(out request))
					{
						if (queueLength >= MinQueueLength)
						{
							break;
						}
						if (!await _blobRequests.Reader.WaitToReadAsync(cancellationToken))
						{
							break;
						}
					}
					else
					{
						Interlocked.Increment(ref _numRequests);

						PacketExport? outputExport;
						if (!TryGetPacketExport(request, out outputExport))
						{
							await _otherRequests.Writer.WriteAsync(request, cancellationToken);
						}
						else
						{
							BundleHandle bundleHandle = outputExport.PacketHandle.Bundle;
							if (!bundleHandleToExportBatch.TryGetValue(bundleHandle, out List<PacketExport>? existingExportBatch))
							{
								existingExportBatch = new List<PacketExport>();
								bundleHandleToExportBatch.Add(bundleHandle, existingExportBatch);
								bundleQueue.Enqueue(bundleHandle);
							}

							existingExportBatch.Add(outputExport);
							queueLength++;
						}
					}
				}

				// Exit once we've processed everything and can't get any more items to read.
				if (queueLength == 0)
				{
					_bundleRequests.Writer.TryComplete();
					_otherRequests.Writer.TryComplete();
					break;
				}

				// Flush the first queue
				{
					BundleHandle bundleHandle = bundleQueue.Dequeue();
					List<PacketExport> exportBatch = bundleHandleToExportBatch[bundleHandle];
					queueLength -= exportBatch.Count;
					bundleHandleToExportBatch.Remove(bundleHandle);

					await WriteBundleRequestsAsync(bundleHandle, exportBatch, cancellationToken);
				}
			}
		}

		async Task WriteBundleRequestsAsync(BundleHandle bundleHandle, List<PacketExport> exports, CancellationToken cancellationToken)
		{
			Interlocked.Increment(ref _numBundles);

			// Group the reads by packet
			List<PacketRequest> packetRequests = new List<PacketRequest>();
			foreach (IGrouping<int, PacketExport> group in exports.GroupBy(x => x.PacketHandle.PacketOffset))
			{
				List<ExportRequest> exportRequests = group.Select(x => new ExportRequest(x.ExportHandle, x.Context)).ToList();
				PacketRequest packetRequest = new PacketRequest(group.First().PacketHandle, exportRequests);
				packetRequests.Add(packetRequest);
			}
			packetRequests.SortBy(x => x.MinOffset);

			// Split the packet requests into contiguous bundle reads
			for (int maxIdx = 0; maxIdx < packetRequests.Count; maxIdx++)
			{
				int minIdx = maxIdx;
				while (maxIdx + 1 < packetRequests.Count && packetRequests[maxIdx + 1].MinOffset < packetRequests[maxIdx].MaxOffset + CoalesceReadsBelowSize)
				{
					maxIdx++;
				}

				int minOffset = packetRequests[minIdx].MinOffset;
				int maxOffset = packetRequests[maxIdx].MaxOffset;

				BundleRequest request = new BundleRequest(bundleHandle, minOffset, maxOffset, packetRequests.GetRange(minIdx, (maxIdx + 1) - minIdx));
				await _bundleRequests.Writer.WriteAsync(request, cancellationToken);
			}
		}

		static bool TryGetPacketExport(BatchReadRequest<TContext> chunk, [NotNullWhen(true)] out PacketExport? export)
		{
			if (chunk.BlobRef.Innermost is ExportHandle exportHandle && exportHandle.Packet is FlushedPacketHandle packetHandle)
			{
				export = new PacketExport(packetHandle, exportHandle, chunk.Context);
				return true;
			}
			else
			{
				export = null;
				return false;
			}
		}

		#endregion
		#region Bundle Reads

		async Task ReadBundlesAsync(CancellationToken cancellationToken)
		{
			await foreach (BundleRequest request in _bundleRequests.Reader.ReadAllAsync(cancellationToken))
			{
				IReadOnlyMemoryOwner<byte>? data = null;
				try
				{
					data = await request.Handle.ReadAsync(request.MinOffset, request.MaxOffset - request.MinOffset, cancellationToken);
					Interlocked.Increment(ref _numReads);
					Interlocked.Add(ref _bytesRead, data.Memory.Length);
#pragma warning disable CA2000
					BundleResponse response = new BundleResponse(data, request.MinOffset, request.MaxOffset, request.Packets);
					await _bundleResponses.Writer.WriteAsync(response, cancellationToken);
#pragma warning restore CA2000
				}
				catch
				{
					data?.Dispose();
					throw;
				}
			}
		}

		#endregion
		#region Packet reads

		async Task ReadPacketsAsync(CancellationToken cancellationToken)
		{
			await foreach (BundleResponse bundleResponse in _bundleResponses.Reader.ReadAllAsync(cancellationToken))
			{
				using IDisposable lifetime = bundleResponse;
				foreach (PacketRequest packetRequest in bundleResponse.Packets)
				{
					// Decode the data for this packet
					ReadOnlyMemory<byte> memory = bundleResponse.Data.Memory.Slice(packetRequest.Handle.PacketOffset - bundleResponse.MinOffset, packetRequest.Handle.PacketLength);

					using PacketReader packetReader = packetRequest.Handle.CreatePacketReader(memory);
					Interlocked.Increment(ref _numPackets);
					Interlocked.Add(ref _bytesDecoded, packetReader.Packet.Length);

					// Create responses for each blob
					BatchReadResponseItem<TContext>[] items = new BatchReadResponseItem<TContext>[packetRequest.Exports.Count];
					try
					{
#pragma warning disable CA2000
						for (int idx = 0; idx < items.Length; idx++)
						{
							ExportRequest exportRequest = packetRequest.Exports[idx];
							BlobData blobData = packetReader.ReadExport(exportRequest.Handle.ExportIdx);
							items[idx] = new BatchReadResponseItem<TContext>(blobData, exportRequest.Context);
						}

						BatchReadResponse<TContext> response = new BatchReadResponse<TContext>(items);
						await _blobResponses.Writer.WriteAsync(response, cancellationToken);
#pragma warning restore CA2000
					}
					catch
					{
						for (int idx = 0; idx < items.Length; idx++)
						{
							items[idx]?.Dispose();
						}
						throw;
					}
				}
			}
		}

		#endregion
	}

	/// <summary>
	/// Extension methods for <see cref="BatchReader{TContext}"/>
	/// </summary>
	public static class BatchReaderExtensions
	{
		/// <summary>
		/// Add tasks for reading to the given async pipeline 
		/// </summary>
		public static void AddBatchReaderTasks<T>(this AsyncPipeline pipeline, int numReadTasks, int numDecodeTasks, BatchReader<T> reader)
			=> reader.AddToPipeline(pipeline, numReadTasks, numDecodeTasks);
	}
}
