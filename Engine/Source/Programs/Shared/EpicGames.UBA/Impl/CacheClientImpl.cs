// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;

namespace EpicGames.UBA
{
	internal class RootPathsImpl : IRootPaths
	{
		IntPtr _handle = IntPtr.Zero;
		readonly ILogger _logger;

		#region DllImport
		[DllImport("UbaHost", CharSet = CharSet.Auto)]
		static extern IntPtr RootPaths_Create(IntPtr logWriter);

		[DllImport("UbaHost", CharSet = CharSet.Auto)]
		static extern void RootPaths_Destroy(IntPtr rootPaths);

		[DllImport("UbaHost", CharSet = CharSet.Auto)]
		static extern bool RootPaths_RegisterRoot(IntPtr rootPaths, string root, bool includeInKey, byte id);

		[DllImport("UbaHost", CharSet = CharSet.Auto)]
		static extern bool RootPaths_RegisterSystemRoots(IntPtr rootPaths, byte id);
		#endregion

		#region IDisposable
		~RootPathsImpl() => Dispose(false);

		public void Dispose()
		{
			Dispose(true);
			GC.SuppressFinalize(this);
		}

		protected virtual void Dispose(bool disposing)
		{
			if (disposing)
			{
			}

			if (_handle != IntPtr.Zero)
			{
				RootPaths_Destroy(_handle);
				_handle = IntPtr.Zero;
			}
		}
		#endregion

		public RootPathsImpl(ILogger logger)
		{
			_logger = logger;
			_handle = RootPaths_Create(_logger.GetHandle());
		}

		public bool RegisterRoot(string path, bool includeInKey, byte id)
		{
			return RootPaths_RegisterRoot(_handle, path, includeInKey, id);
		}

		public bool RegisterSystemRoots(byte startId)
		{
			return RootPaths_RegisterSystemRoots(_handle, startId);
		}

		public IntPtr GetHandle() => _handle;
	}

	internal class CacheClientImpl : ICacheClient
	{
		IntPtr _handle = IntPtr.Zero;
		readonly ISessionServer _sessionServer;

		public delegate void ExitCallback(IntPtr userData, IntPtr handle);

		#region DllImport
		[DllImport("UbaHost", CharSet = CharSet.Auto)]
		static extern IntPtr ProcessStartInfo_Create(string application, string arguments, string workingDir, string description, uint priorityClass, ulong outputStatsThresholdMs, bool trackInputs, string logFile, ExitCallback? exit);

		[DllImport("UbaHost", CharSet = CharSet.Auto)]
		static extern void ProcessStartInfo_Destroy(IntPtr server);

		[DllImport("UbaHost", CharSet = CharSet.Auto)]
		static extern IntPtr CacheClient_Create(IntPtr session, bool reportMissReason, string crypto);

		[DllImport("UbaHost", CharSet = CharSet.Auto)]
		static extern bool CacheClient_Connect(IntPtr cacheClient, string host, int port);

		[DllImport("UbaHost", CharSet = CharSet.Auto)]
		static extern bool CacheClient_WriteToCache(IntPtr cacheClient, IntPtr rootPaths, uint bucket, IntPtr info, byte[] inputs, uint inputsSize, byte[] outputs, uint outputsSize);

		[DllImport("UbaHost", CharSet = CharSet.Auto)]
		static extern IntPtr CacheClient_FetchFromCache2(IntPtr cacheClient, IntPtr rootPaths, uint bucket, IntPtr info);

		[DllImport("UbaHost", CharSet = CharSet.Auto)]
		static extern IntPtr CacheResult_GetLogLine(IntPtr result, uint index);

		[DllImport("UbaHost", CharSet = CharSet.Auto)]
		static extern uint CacheResult_GetLogLineType(IntPtr result, uint index);

		[DllImport("UbaHost", CharSet = CharSet.Auto)]
		static extern void CacheResult_Delete(IntPtr result);

		[DllImport("UbaHost", CharSet = CharSet.Auto)]
		static extern void CacheClient_RequestServerShutdown(IntPtr cacheClient, string reason);

		[DllImport("UbaHost", CharSet = CharSet.Auto)]
		static extern void CacheClient_Destroy(IntPtr cacheClient);
		#endregion

		#region IDisposable
		~CacheClientImpl() => Dispose(false);

		public void Dispose()
		{
			Dispose(true);
			GC.SuppressFinalize(this);
		}

		protected virtual void Dispose(bool disposing)
		{
			if (disposing)
			{
			}

			if (_handle != IntPtr.Zero)
			{
				CacheClient_Destroy(_handle);
				_handle = IntPtr.Zero;
			}
		}
		#endregion

		public CacheClientImpl(ISessionServer server, bool reportMissReason, string crypto)
		{
			_sessionServer = server;
			_handle = CacheClient_Create(_sessionServer.GetHandle(), reportMissReason, crypto);
		}

		public bool Connect(string host, int port)
		{
			return CacheClient_Connect(_handle, host, port);
		}

		public bool WriteToCache(IRootPaths rootPaths, uint bucket, IProcess process, byte[] inputs, uint inputsSize, byte[] outputs, uint outputsSize)
		{
			return CacheClient_WriteToCache(_handle, rootPaths.GetHandle(), bucket, process.GetHandle(), inputs, inputsSize, outputs, outputsSize);
		}

		public FetchFromCacheResult FetchFromCache(IRootPaths rootPaths, uint bucket, ProcessStartInfo info)
		{
			IntPtr si = ProcessStartInfo_Create(info.Application, info.Arguments, info.WorkingDirectory, info.Description, (uint)info.Priority, info.OutputStatsThresholdMs, info.TrackInputs, info.LogFile ?? String.Empty, null);
			IntPtr result = CacheClient_FetchFromCache2(_handle, rootPaths.GetHandle(), bucket, si);
			ProcessStartInfo_Destroy(si);

			if (result == IntPtr.Zero)
			{
				return new FetchFromCacheResult(false, new List<string>());
			}

			string? line = Marshal.PtrToStringAuto(CacheResult_GetLogLine(result, 0));
			if (line == null)
			{
				return new FetchFromCacheResult(true, new List<string>());
			}

			List<string> logLines = new();
			while (line != null)
			{
				logLines.Add(line);
				line = Marshal.PtrToStringAuto(CacheResult_GetLogLine(result, (uint)logLines.Count));
			}


			return new FetchFromCacheResult(true, logLines);
		}

		public void RequestServerShutdown(string reason)
		{
			CacheClient_RequestServerShutdown(_handle, reason);
		}

		public IntPtr GetHandle() => _handle;
	}
}
