// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Text;
using System.Threading;
using System.Threading.Channels;
using System.Threading.Tasks;
using EpicGames.Core;
using EpicGames.Horde.Storage;
using Google.Protobuf;
using Grpc.Core;
using Horde.Common.Rpc;
using Microsoft.Extensions.Logging;

namespace EpicGames.Horde.Logs
{
	/// <summary>
	/// Class to handle uploading log data to the server in the background
	/// </summary>
	sealed class ServerLogger : IServerLogger
	{
		const int FlushLength = 1024 * 1024;

		readonly IHordeClient _hordeClient;
		readonly LogId _logId;
		readonly LogLevel _minimumLevel;
		readonly ILogger _internalLogger;
		readonly LogBuilder _builder;
		readonly IStorageNamespace _store;
		readonly IBlobWriter _writer;

		int _bufferLength;

		// Tailing task
		readonly Task _tailTask;
		AsyncEvent _tailTaskStop;
		readonly AsyncEvent _newTailDataEvent = new AsyncEvent();

		readonly Channel<JsonLogEvent> _dataChannel;
		Task? _dataWriter;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="hordeClient">Horde instance to write to</param>
		/// <param name="logId">The log id to write to</param>
		/// <param name="minimumLevel">Minimum level for output</param>
		/// <param name="internalLogger">Logger for systemic messages</param>
		public ServerLogger(IHordeClient hordeClient, LogId logId, LogLevel minimumLevel, ILogger internalLogger)
		{
			_hordeClient = hordeClient;
			_logId = logId;
			_minimumLevel = minimumLevel;
			_internalLogger = internalLogger;
			_builder = new LogBuilder(LogFormat.Json, internalLogger);
			_store = _hordeClient.GetStorageNamespace(logId);
			_writer = _store.CreateBlobWriter();

			_tailTaskStop = new AsyncEvent();
			_tailTask = Task.Run(() => TickTailAsync());

			_dataChannel = Channel.CreateUnbounded<JsonLogEvent>();
			_dataWriter = Task.Run(() => RunDataWriterAsync());
		}

		/// <inheritdoc/>
		public void Log<TState>(LogLevel logLevel, EventId eventId, TState state, Exception? exception, Func<TState, Exception?, string> formatter)
		{
			JsonLogEvent jsonLogEvent = JsonLogEvent.FromLoggerState(logLevel, eventId, state, exception, formatter);
			WriteFormattedEvent(jsonLogEvent);
		}

		/// <inheritdoc/>
		public bool IsEnabled(LogLevel logLevel) => logLevel >= _minimumLevel;

		/// <inheritdoc/>
		public IDisposable BeginScope<TState>(TState state)
			=> null!;

		private void WriteFormattedEvent(JsonLogEvent jsonLogEvent)
		{
			if (!_dataChannel.Writer.TryWrite(jsonLogEvent))
			{
				throw new InvalidOperationException("Expected unbounded writer to complete immediately");
			}
		}

		/// <summary>
		/// Stops the log writer's background task
		/// </summary>
		/// <returns>Async task</returns>
		public async Task StopAsync()
		{
			if (_dataWriter != null)
			{
				_dataChannel.Writer.TryComplete();
				await _dataWriter;
				_dataWriter = null;
			}
		}

		/// <summary>
		/// Dispose of this object. Call StopAsync() to stop asynchronously.
		/// </summary>
		public async ValueTask DisposeAsync()
		{
			await StopAsync();

			_internalLogger.LogInformation("Disposing json log task");

			if (_tailTaskStop != null)
			{
				_tailTaskStop.Latch();
				_newTailDataEvent.Latch();

				await _tailTask;
				_tailTaskStop = null!;
			}

			if (_writer != null)
			{
				await _writer.DisposeAsync();
			}
		}

		/// <summary>
		/// Upload the log data to the server in the background
		/// </summary>
		/// <returns>Async task</returns>
		async Task RunDataWriterAsync()
		{
			// Current position and line number in the log file
			long packetOffset = 0;
			int packetLineIndex = 0;

			// Index of the next line to write to the log
			int nextLineIndex = 0;

			// Total number of errors and warnings
			const int MaxErrors = 200;
			int numErrors = 0;
			const int MaxWarnings = 200;
			int numWarnings = 0;

			// Buffer for events read in a single iteration
			ServerLogPacketBuilder writer = new ServerLogPacketBuilder();
			List<RpcCreateLogEventRequest> events = new List<RpcCreateLogEventRequest>();

			// Whether we've written the flush command
			for (; ; )
			{
				events.Clear();

				// Get the next data
				Task waitTask = Task.Delay(TimeSpan.FromSeconds(2.0));
				while (writer.PacketLength < writer.MaxPacketLength)
				{
					JsonLogEvent jsonLogEvent;
					if (_dataChannel.Reader.TryRead(out jsonLogEvent))
					{
						int lineCount = writer.SanitizeAndWriteEvent(jsonLogEvent);
						if (jsonLogEvent.LineIndex == 0)
						{
							if (jsonLogEvent.Level == LogLevel.Warning && ++numWarnings <= MaxWarnings)
							{
								AddEvent(jsonLogEvent.Data.Span, nextLineIndex, Math.Max(lineCount, jsonLogEvent.LineCount), LogEventSeverity.Warning, events);
							}
							else if ((jsonLogEvent.Level == LogLevel.Error || jsonLogEvent.Level == LogLevel.Critical) && ++numErrors <= MaxErrors)
							{
								AddEvent(jsonLogEvent.Data.Span, nextLineIndex, Math.Max(lineCount, jsonLogEvent.LineCount), LogEventSeverity.Error, events);
							}
						}
						nextLineIndex += lineCount;
					}
					{
						Task<bool> readTask = _dataChannel.Reader.WaitToReadAsync().AsTask();
						if (await Task.WhenAny(readTask, waitTask) == waitTask)
						{
							break;
						}
						if (!await readTask)
						{
							break;
						}
					}
				}

				// Upload it to the server
				if (writer.PacketLength > 0)
				{
					(ReadOnlyMemory<byte> packet, int packetLineCount) = writer.CreatePacket();
					try
					{
						await WriteOutputAsync(packet, false, CancellationToken.None);
						packetOffset += packet.Length;
						packetLineIndex += packetLineCount;
					}
					catch (Exception ex)
					{
						_internalLogger.LogWarning(ex, "Unable to write data to server (log {LogId}, offset {Offset}, length {Length}, lines {StartLine}-{EndLine})", _logId, packetOffset, packet.Length, packetLineIndex, packetLineIndex + packetLineCount);
					}
				}

				// Write all the events
				if (events.Count > 0)
				{
					try
					{
						await WriteEventsAsync(events, CancellationToken.None);
					}
					catch (Exception ex)
					{
						_internalLogger.LogWarning(ex, "Unable to create events");
					}
				}

				// Wait for more data to be available
				if (writer.PacketLength <= 0 && !await _dataChannel.Reader.WaitToReadAsync())
				{
					try
					{
						await WriteOutputAsync(ReadOnlyMemory<byte>.Empty, true, CancellationToken.None);
					}
					catch (Exception ex)
					{
						_internalLogger.LogWarning(ex, "Unable to flush data to server (log {LogId}, offset {Offset})", _logId, packetOffset);
					}
					break;
				}
			}
		}

		void AddEvent(ReadOnlySpan<byte> span, int lineIndex, int lineCount, LogEventSeverity severity, List<RpcCreateLogEventRequest> events)
		{
			try
			{
				events.Add(new RpcCreateLogEventRequest { Severity = (int)severity, LogId = _logId.ToString(), LineIndex = lineIndex, LineCount = lineCount });
			}
			catch (Exception ex)
			{
				_internalLogger.LogError(ex, "Exception while trying to parse line count from data ({Message})", Encoding.UTF8.GetString(span));
			}
		}

		async Task TickTailAsync()
		{
			for (; ; )
			{
				try
				{
					await TickTailInternalAsync();
					break;
				}
				catch (OperationCanceledException ex)
				{
					_internalLogger.LogInformation(ex, "Cancelled log tailing task");
					break;
				}
				catch (Exception ex)
				{
					_internalLogger.LogError(ex, "Exception on log tailing task ({LogId}): {Message}", _logId, ex.Message);
					await Task.Delay(TimeSpan.FromSeconds(10.0));
				}
			}
		}

		async Task TickTailInternalAsync()
		{
			int tailNext = -1;
			Task tickTask = Task.CompletedTask;
			while (!_tailTaskStop.IsSet())
			{
				Task newTailDataTask = _newTailDataEvent.Task;
				int initialTailNext = tailNext;

				// Get the data to send to the server
				ReadOnlyMemory<byte> tailData = ReadOnlyMemory<byte>.Empty;
				if (tailNext != -1)
				{
					(tailNext, tailData) = _builder.ReadTailData(tailNext, 16 * 1024);
				}

				// If we don't have any updates for the server, wait until we do. We need to ensure
				// we keep pumping the RPC with the server in case the requested tail next value changes,
				// and to make sure that we don't expire the existing tail data.
				if (tailNext != -1 && tailData.IsEmpty && tailNext == initialTailNext && !tickTask.IsCompleted)
				{
					_internalLogger.LogInformation("No tail data available for log {LogId} after line {TailNext}; waiting for more...", _logId, tailNext);
					await Task.WhenAny(newTailDataTask, tickTask);
					continue;
				}

				string start = "";
				if (tailData.Length > 0)
				{
					start = Encoding.UTF8.GetString(tailData.Slice(0, Math.Min(tailData.Length, 256)).Span);
				}

				// Update the next tailing position
				int numLines = CountLines(tailData.Span);
				_internalLogger.LogInformation("Setting log {LogId} tail = {TailNext}, data = {TailDataSize} bytes, {NumLines} lines ('{Start}')", _logId, tailNext, tailData.Length, numLines, start);

				int newTailNext = await UpdateLogTailAsync(tailNext, tailData, CancellationToken.None);
				_internalLogger.LogInformation("Log {LogId} tail next = {TailNext}", _logId, newTailNext);

				if (newTailNext != tailNext)
				{
					tailNext = newTailNext;
					_internalLogger.LogInformation("Modified tail position for log {LogId} to {TailNext}", _logId, tailNext);
				}

				tickTask = Task.Delay(TimeSpan.FromSeconds(10.0));
			}
			_internalLogger.LogInformation("Finishing log tail task");
		}

		static int CountLines(ReadOnlySpan<byte> data)
		{
			int lines = 0;
			for (int idx = 0; idx < data.Length; idx++)
			{
				if (data[idx] == '\n')
				{
					lines++;
				}
			}
			return lines;
		}

		/// <inheritdoc/>
		public async Task WriteEventsAsync(List<RpcCreateLogEventRequest> events, CancellationToken cancellationToken)
		{
			LogRpc.LogRpcClient logRpc = await _hordeClient.CreateGrpcClientAsync<LogRpc.LogRpcClient>(cancellationToken);

			RpcCreateLogEventsRequest request = new RpcCreateLogEventsRequest();
			request.Events.AddRange(events);

			await logRpc.CreateLogEventsAsync(request, cancellationToken: cancellationToken);
		}

		/// <inheritdoc/>
		public async Task WriteOutputAsync(ReadOnlyMemory<byte> data, bool flush, CancellationToken cancellationToken)
		{
			_builder.WriteData(data);
			_bufferLength += data.Length;

			if (flush || _bufferLength > FlushLength)
			{
				IHashedBlobRef<LogNode> target = await _builder.FlushAsync(_writer, flush, cancellationToken);
				await UpdateLogAsync(target, _builder.LineCount, flush, cancellationToken);
				_bufferLength = 0;
			}

			_newTailDataEvent.Set();
		}

		#region RPC calls

		async Task UpdateLogAsync(IHashedBlobRef target, int lineCount, bool complete, CancellationToken cancellationToken)
		{
			_internalLogger.LogInformation("Updating log {LogId} to line {LineCount}, target {Locator}", _logId, lineCount, target.GetLocator());

			RpcUpdateLogRequest request = new RpcUpdateLogRequest();
			request.LogId = _logId.ToString();
			request.LineCount = lineCount;
			request.TargetHash = target.Hash.ToString();
			request.TargetLocator = target.GetLocator().ToString();
			request.Complete = complete;

			LogRpc.LogRpcClient clientRef = await _hordeClient.CreateGrpcClientAsync<LogRpc.LogRpcClient>(cancellationToken);
			await clientRef.UpdateLogAsync(request, cancellationToken: cancellationToken);
		}

		async Task<int> UpdateLogTailAsync(int tailNext, ReadOnlyMemory<byte> tailData, CancellationToken cancellationToken)
		{
			DateTime deadline = DateTime.UtcNow.AddMinutes(2.0);
			try
			{
				LogRpc.LogRpcClient clientRef = await _hordeClient.CreateGrpcClientAsync<LogRpc.LogRpcClient>(cancellationToken);
				using AsyncDuplexStreamingCall<RpcUpdateLogTailRequest, RpcUpdateLogTailResponse> call = clientRef.UpdateLogTail(deadline: deadline, cancellationToken: cancellationToken);

				// Write the request to the server
				RpcUpdateLogTailRequest request = new RpcUpdateLogTailRequest();
				request.LogId = _logId.ToString();
				request.TailNext = tailNext;
				request.TailData = UnsafeByteOperations.UnsafeWrap(tailData);
				await call.RequestStream.WriteAsync(request, cancellationToken);
				_internalLogger.LogInformation("Writing log data: {LogId}, {TailNext}, {TailData} bytes", _logId, tailNext, tailData.Length);

				// Wait until the server responds or we need to trigger a new update
				Task<bool> moveNextAsync = call.ResponseStream.MoveNext();

				Task task = await Task.WhenAny(moveNextAsync, _tailTaskStop.Task, Task.Delay(TimeSpan.FromMinutes(1.0), CancellationToken.None));
				if (task == _tailTaskStop.Task)
				{
					_internalLogger.LogInformation("Cancelling long poll from client side (complete)");
				}

				// Close the request stream to indicate that we're finished
				await call.RequestStream.CompleteAsync();

				// Wait for a response or a new update to come in, then close the request stream
				RpcUpdateLogTailResponse? response = null;
				while (await moveNextAsync)
				{
					response = call.ResponseStream.Current;
					moveNextAsync = call.ResponseStream.MoveNext();
				}
				return response?.TailNext ?? -1;
			}
			catch (RpcException ex) when (ex.StatusCode == StatusCode.DeadlineExceeded)
			{
				_internalLogger.LogDebug(ex, "Log tail deadline exceeded, ignoring.");
				return -1;
			}
		}

		#endregion
	}
}
