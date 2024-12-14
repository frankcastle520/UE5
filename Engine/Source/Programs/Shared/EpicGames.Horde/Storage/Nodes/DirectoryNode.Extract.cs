// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.IO.MemoryMappedFiles;
using System.Linq;
using System.Threading;
using System.Threading.Channels;
using System.Threading.Tasks;
using EpicGames.Core;
using Microsoft.Extensions.Logging;

namespace EpicGames.Horde.Storage.Nodes
{
	/// <summary>
	/// Stats reported for copy operations
	/// </summary>
	public interface IExtractStats
	{
		/// <summary>
		/// Number of files that have been copied
		/// </summary>
		int NumFiles { get; }

		/// <summary>
		/// Total size of data to be copied
		/// </summary>
		long ExtractSize { get; }

		/// <summary>
		/// Processing speed, in bytes per second
		/// </summary>
		double ExtractRate { get; }

		/// <summary>
		/// Total size of the data downloaded
		/// </summary>
		long DownloadSize { get; }

		/// <summary>
		/// Download speed, in bytes per second
		/// </summary>
		double DownloadRate { get; }
	}

	/// <summary>
	/// Progress logger for writing copy stats
	/// </summary>
	public class ExtractStatsLogger : IProgress<IExtractStats>
	{
		readonly int _totalCount;
		readonly long _totalSize;
		readonly ILogger _logger;

		/// <summary>
		/// Whether to print out separate stats for download speed
		/// </summary>
		public bool ShowDownloadStats { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		public ExtractStatsLogger(ILogger logger)
			=> _logger = logger;

		/// <summary>
		/// Constructor
		/// </summary>
		public ExtractStatsLogger(int totalCount, long totalSize, ILogger logger)
		{
			_totalCount = totalCount;
			_totalSize = totalSize;
			_logger = logger;
		}

		/// <inheritdoc/>
		public void Report(IExtractStats stats)
		{
			if (ShowDownloadStats)
			{
				double downloadRate = (stats.DownloadRate * 8.0) / (1024.0 * 1024.0);
				_logger.LogInformation("Downloaded {TotalSize:n1}mb, {Rate:n0} mbps", stats.DownloadSize / (1024.0 * 1024.0), downloadRate);
			}

			if (_totalCount > 0 && _totalSize > 0)
			{
				_logger.LogInformation("Written {NumFiles:n0}/{TotalFiles:n0} files ({Size:n1}/{TotalSize:n1}mb, {Rate:n1}mb/s, {Pct}%)", stats.NumFiles, _totalCount, stats.ExtractSize / (1024.0 * 1024.0), _totalSize / (1024.0 * 1024.0), stats.ExtractRate / (1024.0 * 1024.0), (int)((Math.Max(stats.ExtractSize, 1) * 100) / Math.Max(_totalSize, 1)));
			}
			else if (_totalCount > 0)
			{
				_logger.LogInformation("Written {NumFiles:n0}/{TotalFiles:n0} files ({Size:n1}mb, {Rate:n1}mb/s)", stats.NumFiles, _totalCount, stats.ExtractSize / (1024.0 * 1024.0), stats.ExtractRate / (1024.0 * 1024.0));
			}
			else if (_totalSize > 0)
			{
				_logger.LogInformation("Written {NumFiles:n0} files ({Size:n1}/{TotalSize:n1}mb, {Rate:n1}mb/s, {Pct}%)", stats.NumFiles, stats.ExtractSize / (1024.0 * 1024.0), _totalSize / (1024.0 * 1024.0), stats.ExtractRate / (1024.0 * 1024.0), (int)((Math.Max(stats.ExtractSize, 1) * 100) / Math.Max(_totalSize, 1)));
			}
			else
			{
				_logger.LogInformation("Written {NumFiles:n0} files ({Size:n1}mb, {Rate:n1}mb/s)", stats.NumFiles, stats.ExtractSize / (1024.0 * 1024.0), stats.ExtractRate / (1024.0 * 1024.0));
			}
		}
	}

	/// <summary>
	/// Extension methods for extracting data from directory nodes
	/// </summary>
	public static class DirectoryNodeExtract
	{
		/// <summary>
		/// Utility function to allow extracting a packed directory to disk
		/// </summary>
		/// <param name="directoryRef">Directory to update</param>
		/// <param name="directoryInfo"></param>
		/// <param name="logger"></param>
		/// <param name="cancellationToken"></param>
		public static Task ExtractAsync(this IBlobRef<DirectoryNode> directoryRef, DirectoryInfo directoryInfo, ILogger logger, CancellationToken cancellationToken)
			=> ExtractAsync(directoryRef, directoryInfo, null, logger, cancellationToken);

		class OutputStats
		{
			public int _writtenFiles;
			public long _writtenBytes;
		}

		class OutputFile
		{
			public string Path { get; }
			public FileInfo FileInfo { get; }
			public FileEntry FileEntry { get; }

			bool _createdFile;
			int _remainingChunks;

			public OutputFile(string path, FileInfo fileInfo, FileEntry fileEntry)
			{
				Path = path;
				FileInfo = fileInfo;
				FileEntry = fileEntry;
			}

			public int IncrementRemaining() => Interlocked.Increment(ref _remainingChunks);
			public int DecrementRemaining() => Interlocked.Decrement(ref _remainingChunks);

			public FileStream OpenStream()
			{
				lock (FileEntry)
				{
					if (!_createdFile)
					{
						if (FileInfo.Exists)
						{
							if (FileInfo.LinkTarget != null)
							{
								FileInfo.Delete();
							}
							else if (FileInfo.IsReadOnly)
							{
								FileInfo.IsReadOnly = false;
							}
						}
						else
						{
							FileInfo.Directory?.Create();
						}
					}

					FileStream? stream = null;
					try
					{
						try
						{
							stream = FileInfo.Open(FileMode.OpenOrCreate, FileAccess.ReadWrite, FileShare.ReadWrite);
						}
						catch (IOException ex)
						{
							string? lockInfo = FileUtils.GetFileLockInfo(FileInfo.FullName);
							if (lockInfo == null)
							{
								throw;
							}
							else
							{
								throw new WrappedFileOrDirectoryException(ex, $"{ex.Message}\n{lockInfo}");
							}
						}

						if (!_createdFile)
						{
							stream.SetLength(FileEntry.Length);
							_createdFile = true;
						}
						return stream;
					}
					catch
					{
						stream?.Dispose();
						throw;
					}
				}
			}
		}

		record class OutputChunk(OutputFile File, long Offset, long Length, IBlobRef Handle);

		record class OutputBatch(List<OutputChunk> Chunks);

		// Writes output chunks to a channel. Buffers one chunk until FlushAsync() is called to ensure
		// the remaining chunk reference count doesn't reach zero until the last chunk has been processed.
		class OutputChunkWriter
		{
			public OutputFile OutputFile { get; }

			readonly ChannelWriter<BatchReadRequest<OutputChunk>> _chunkWriter;
			BatchReadRequest<OutputChunk>? _bufferedChunk;

			public OutputChunkWriter(OutputFile file, ChannelWriter<BatchReadRequest<OutputChunk>> chunkWriter)
			{
				OutputFile = file;
				_chunkWriter = chunkWriter;
			}

			public async Task WriteAsync(long offset, long length, IBlobRef handle, CancellationToken cancellationToken)
			{
				if (_bufferedChunk != null)
				{
					await _chunkWriter.WriteAsync(_bufferedChunk, cancellationToken);
				}

				OutputFile.IncrementRemaining();
				_bufferedChunk = new BatchReadRequest<OutputChunk>(handle, new OutputChunk(OutputFile, offset, length, handle));
			}

			public async Task FlushAsync(CancellationToken cancellationToken)
			{
				if (_bufferedChunk != null)
				{
					await _chunkWriter.WriteAsync(_bufferedChunk, cancellationToken);
					_bufferedChunk = null;
				}
			}
		}

#pragma warning disable IDE0060
		static void TraceBlobRead(string type, string path, IBlobRef handle, ILogger logger)
		{
			//			logger.LogTrace(KnownLogEvents.Horde_BlobRead, "Blob [{Type,-20}] Path=\"{Path}\", Locator={Locator}", type, path, handle.GetLocator());
		}
#pragma warning restore IDE0060

		/// <summary>
		/// Utility function to allow extracting a packed directory to disk
		/// </summary>
		/// <param name="directoryRef">Directory to extract</param>
		/// <param name="directoryInfo">Direcotry to write to</param>
		/// <param name="progress">Sink for progress updates</param>
		/// <param name="logger">Logger for output</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		public static Task ExtractAsync(this IBlobRef<DirectoryNode> directoryRef, DirectoryInfo directoryInfo, IProgress<IExtractStats>? progress, ILogger logger, CancellationToken cancellationToken)
		{
			return ExtractAsync(directoryRef, directoryInfo, progress, TimeSpan.FromSeconds(5.0), logger, cancellationToken);
		}

		/// <summary>
		/// Utility function to allow extracting a packed directory to disk
		/// </summary>
		/// <param name="directoryRef">Directory to extract</param>
		/// <param name="directoryInfo">Direcotry to write to</param>
		/// <param name="progress">Sink for progress updates</param>
		/// <param name="frequency">Frequency for progress updates</param>
		/// <param name="logger">Logger for output</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		public static async Task ExtractAsync(this IBlobRef<DirectoryNode> directoryRef, DirectoryInfo directoryInfo, IProgress<IExtractStats>? progress, TimeSpan frequency, ILogger logger, CancellationToken cancellationToken)
		{
			DirectoryNode directoryNode = await directoryRef.ReadBlobAsync(cancellationToken);
			await ExtractAsync(directoryNode, directoryInfo, progress, frequency, logger, cancellationToken);
		}

		/// <summary>
		/// Utility function to allow extracting a packed directory to disk
		/// </summary>
		/// <param name="directoryNode">Directory to extract</param>
		/// <param name="directoryInfo">Direcotry to write to</param>
		/// <param name="logger">Logger for output</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		public static Task ExtractAsync(this DirectoryNode directoryNode, DirectoryInfo directoryInfo, ILogger logger, CancellationToken cancellationToken)
			=> ExtractAsync(directoryNode, directoryInfo, null, TimeSpan.FromDays(1.0), logger, cancellationToken);

		/// <summary>
		/// Utility function to allow extracting a packed directory to disk
		/// </summary>
		/// <param name="directoryNode">Directory to extract</param>
		/// <param name="directoryInfo">Direcotry to write to</param>
		/// <param name="progress">Sink for progress updates</param>
		/// <param name="frequency">Frequency for progress updates</param>
		/// <param name="logger">Logger for output</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		public static async Task ExtractAsync(this DirectoryNode directoryNode, DirectoryInfo directoryInfo, IProgress<IExtractStats>? progress, TimeSpan frequency, ILogger logger, CancellationToken cancellationToken)
		{
			int numReadTasks = 16;
			int numDecodeTasks = Math.Min(Environment.ProcessorCount, 16);
			int numWriteTasks = Math.Min(1 + (int)(directoryNode.Length / (16 * 1024 * 1024)), 16);
			logger.LogInformation("Using {NumReadTasks} read tasks, {NumDecodeTasks} decode tasks, {NumWriteTasks} write tasks", numReadTasks, numDecodeTasks, numWriteTasks);

			await using (AsyncPipeline pipeline = new AsyncPipeline(cancellationToken))
			{
				using BatchReader<OutputChunk> batchReader = new BatchReader<OutputChunk>();

				_ = pipeline.AddTask(ctx => FindOutputChunksRootAsync(directoryInfo, directoryNode, batchReader.RequestWriter, logger, ctx));
				pipeline.AddBatchReaderTasks(numReadTasks, numDecodeTasks, batchReader);

				OutputStats outputStats = new OutputStats();
				Task[] writeTasks = pipeline.AddTasks(numWriteTasks, ctx => WriteAsync(batchReader.ResponseReader, outputStats, logger, ctx));

				if (progress != null)
				{
					_ = pipeline.AddTask(ctx => UpdateStatsAsync(batchReader, outputStats, Task.WhenAll(writeTasks), progress, frequency, ctx));
				}

				await pipeline.WaitForCompletionAsync();

				cancellationToken.ThrowIfCancellationRequested();
			}
		}

		#region Enumerate chunks

		static async Task FindOutputChunksRootAsync(DirectoryInfo rootDir, DirectoryNode node, ChannelWriter<BatchReadRequest<OutputChunk>> chunks, ILogger logger, CancellationToken cancellationToken)
		{
			await FindOutputChunksForDirectoryAsync(rootDir, "", node, chunks, logger, cancellationToken);
			chunks.Complete();
		}

		static async Task FindOutputChunksForDirectoryAsync(DirectoryInfo rootDir, string path, DirectoryNode node, ChannelWriter<BatchReadRequest<OutputChunk>> chunks, ILogger logger, CancellationToken cancellationToken)
		{
			foreach (FileEntry fileEntry in node.Files)
			{
				string filePath = CombinePaths(path, fileEntry.Name);
				FileInfo fileInfo = new FileInfo(Path.Combine(rootDir.FullName, filePath));
				OutputFile outputFile = new OutputFile(filePath, fileInfo, fileEntry);

				await FindOutputChunksForFileAsync(outputFile, chunks, logger, cancellationToken);
			}

			foreach (DirectoryEntry directoryEntry in node.Directories)
			{
				string subPath = CombinePaths(path, directoryEntry.Name);
				TraceBlobRead("Directory", subPath, directoryEntry.Handle, logger);
				DirectoryNode subDirectoryNode = await directoryEntry.Handle.ReadBlobAsync(cancellationToken);

				await FindOutputChunksForDirectoryAsync(rootDir, subPath, subDirectoryNode, chunks, logger, cancellationToken);
			}
		}

		static async Task FindOutputChunksForFileAsync(OutputFile outputFile, ChannelWriter<BatchReadRequest<OutputChunk>> chunks, ILogger logger, CancellationToken cancellationToken)
		{
			OutputChunkWriter outputWriter = new OutputChunkWriter(outputFile, chunks);
			await FindOutputChunksAsync(outputWriter, 0, outputFile.FileEntry.Target, logger, cancellationToken);
			await outputWriter.FlushAsync(cancellationToken);
		}

		static async Task<long> FindOutputChunksAsync(OutputChunkWriter chunkWriter, long offset, ChunkedDataNodeRef dataRef, ILogger logger, CancellationToken cancellationToken)
		{
			if (dataRef.Type == ChunkedDataNodeType.Leaf)
			{
				await chunkWriter.WriteAsync(offset, dataRef.Length, dataRef.Handle, cancellationToken);
				if (dataRef.Length < 0)
				{
					// Backwards compatibility hack for v2 format
					using BlobData data = await dataRef.Handle.ReadBlobDataAsync(cancellationToken);
					return data.Data.Length;
				}
				return dataRef.Length;
			}
			else
			{
				using BlobData data = await dataRef.Handle.ReadBlobDataAsync(cancellationToken);
				TraceBlobRead("Interior", chunkWriter.OutputFile.Path, dataRef.Handle, logger);

				if (data.Type.Guid == LeafChunkedDataNodeConverter.BlobType.Guid)
				{
					await chunkWriter.WriteAsync(offset, dataRef.Length, dataRef.Handle, cancellationToken);
					return data.Data.Length;
				}
				else
				{
					long length = 0;

					InteriorChunkedDataNode interiorNode = BlobSerializer.Deserialize<InteriorChunkedDataNode>(data);
					foreach (ChunkedDataNodeRef childRef in interiorNode.Children)
					{
						length += await FindOutputChunksAsync(chunkWriter, offset + length, childRef, logger, cancellationToken);
					}

					return length;
				}
			}
		}

		#endregion

		#region Write to disk

		static async Task WriteAsync(ChannelReader<BatchReadResponse<OutputChunk>> batchReader, OutputStats outputStats, ILogger logger, CancellationToken cancellationToken)
		{
			const int WriteBatchSize = 64;
			while (await batchReader.WaitToReadAsync(cancellationToken))
			{
				BatchReadResponse<OutputChunk>? batch;
				while (batchReader.TryRead(out batch))
				{
					try
					{
						List<Task> tasks = new List<Task>();
						foreach (IReadOnlyList<BatchReadResponseItem<OutputChunk>> group in batch.Items.Batch(WriteBatchSize))
						{
							tasks.Add(WriteChunksAsync(group.ToArray(), outputStats, logger, cancellationToken));
						}
						await Task.WhenAll(tasks);
					}
					finally
					{
						batch.Dispose();
					}
				}
			}
		}

		static async Task WriteChunksAsync(ArraySegment<BatchReadResponseItem<OutputChunk>> chunks, OutputStats outputStats, ILogger logger, CancellationToken cancellationToken)
		{
			for (int chunkIdx = 0; chunkIdx < chunks.Count;)
			{
				OutputFile file = chunks[chunkIdx].Context.File;

				int maxChunkIdx = chunkIdx + 1;
				while (maxChunkIdx < chunks.Count && chunks[maxChunkIdx].Context.File == file)
				{
					maxChunkIdx++;
				}

				try
				{
					await ExtractChunksToFileAsync(file, chunks.Slice(chunkIdx, maxChunkIdx - chunkIdx), outputStats, logger, cancellationToken);
					//await ExtractChunksToNullAsync(file, chunks.Slice(chunkIdx, maxChunkIdx - chunkIdx), outputStats, logger, cancellationToken);
				}
				catch (OperationCanceledException)
				{
					throw;
				}
				catch (Exception ex)
				{
					throw new StorageException($"Unable to extract {file?.FileInfo?.FullName}: {ex.Message}", ex);
				}

				chunkIdx = maxChunkIdx;
			}
		}

		static async Task ExtractChunksToFileAsync(OutputFile file, ArraySegment<BatchReadResponseItem<OutputChunk>> chunks, OutputStats stats, ILogger logger, CancellationToken cancellationToken)
		{
			// Open the file for the current chunk
			int remainingChunks = 0;
			await using (FileStream stream = file.OpenStream())
			{
				if (file.FileEntry.Length == 0)
				{
					// If this file is empty, don't write anything and just move to the next chunk
					for (int idx = 0; idx < chunks.Count; idx++)
					{
						remainingChunks = file.DecrementRemaining();
					}
				}
				else
				{
					// Process as many chunks as we can for this file
					using MemoryMappedFile memoryMappedFile = MemoryMappedFile.CreateFromFile(stream, null, file.FileEntry.Length, MemoryMappedFileAccess.ReadWrite, HandleInheritability.None, false);
					using MemoryMappedView memoryMappedView = new MemoryMappedView(memoryMappedFile, 0, file.FileEntry.Length);

					for (int chunkIdx = 0; chunkIdx < chunks.Count; chunkIdx++)
					{
						BatchReadResponseItem<OutputChunk> chunk = chunks[chunkIdx];
						cancellationToken.ThrowIfCancellationRequested();

						// Write this chunk
						TraceBlobRead("Leaf", chunk.Context.File.Path, chunk.Context.Handle, logger);
						chunk.BlobData.Data.CopyTo(memoryMappedView!.GetMemory(chunk.Context.Offset, chunk.BlobData.Data.Length));

						// Update the stats
						remainingChunks = file.DecrementRemaining();
						Interlocked.Add(ref stats._writtenBytes, chunk.Context.Length);
					}
				}
			}

			// Set correct permissions on the output file
			if (remainingChunks == 0)
			{
				file.FileInfo.Refresh();
				FileEntry.SetPermissions(file.FileInfo!, file.FileEntry.Flags);

				if ((file.FileEntry.Flags & FileEntryFlags.HasModTime) != 0)
				{
					file.FileInfo.LastWriteTimeUtc = file.FileEntry.ModTime;
				}

				Interlocked.Increment(ref stats._writtenFiles);
			}
		}

#pragma warning disable IDE0051
		// Update counters for extracting chunks without writing any data. Useful for profiling bottlenecks in other stages of the pipeline.
		static Task ExtractChunksToNullAsync(OutputFile file, ArraySegment<BatchReadResponseItem<OutputChunk>> chunks, OutputStats stats, ILogger logger, CancellationToken cancellationToken)
		{
			_ = logger;
			_ = cancellationToken;

			foreach (BatchReadResponseItem<OutputChunk> chunk in chunks)
			{
				Interlocked.Add(ref stats._writtenBytes, chunk.Context.Length);

				int remainingChunks = file.DecrementRemaining();
				if (remainingChunks == 0)
				{
					Interlocked.Increment(ref stats._writtenFiles);
				}
			}

			return Task.CompletedTask;
		}
#pragma warning restore IDE0051

		#endregion

		#region Stats

		class ExtractStats : IExtractStats
		{
			public int NumFiles { get; set; }
			public long ExtractSize { get; set; }
			public double ExtractRate { get; set; }
			public long DownloadSize { get; set; }
			public double DownloadRate { get; set; }
		}

		static async Task UpdateStatsAsync(BatchReader<OutputChunk> batchReader, OutputStats outputStats, Task writeTask, IProgress<IExtractStats> progress, TimeSpan frequency, CancellationToken cancellationToken)
		{
			Stopwatch timer = Stopwatch.StartNew();

			long lastExtractSize = 0;
			long lastDownloadSize = 0;

			Task? completeTask = null;
			while (completeTask != writeTask)
			{
				completeTask = await Task.WhenAny(writeTask, Task.Delay(frequency, cancellationToken));

				ExtractStats stats = new ExtractStats();
				stats.NumFiles = Interlocked.CompareExchange(ref outputStats._writtenFiles, 0, 0);
				stats.ExtractSize = Interlocked.CompareExchange(ref outputStats._writtenBytes, 0, 0);

				BatchReaderStats batchStats = batchReader.GetStats();
				stats.DownloadSize = batchStats.BytesRead;

				double elapsedSeconds = timer.Elapsed.TotalSeconds;
				if (elapsedSeconds > 1.0)
				{
					stats.ExtractRate = (stats.ExtractSize - lastExtractSize) / elapsedSeconds;
					stats.DownloadRate = (stats.DownloadSize - lastDownloadSize) / elapsedSeconds;

					lastExtractSize = stats.ExtractSize;
					lastDownloadSize = stats.DownloadSize;

					timer.Restart();
				}

				progress.Report(stats);
			}
		}

		#endregion

		static string CombinePaths(string basePath, string nextPath)
		{
			if (basePath.Length > 0)
			{
				return $"{basePath}/{nextPath}";
			}
			else
			{
				return nextPath;
			}
		}
	}
}
