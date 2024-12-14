// Copyright Epic Games, Inc. All Rights Reserved.

using System.Collections.Generic;

namespace EpicGames.UBA
{
	/// <summary>
	/// Base interface for root paths used by cache system to normalize paths
	/// </summary>
	public interface IRootPaths : IBaseInterface
	{
		/// <summary>
		/// Register roots used to normalize paths in caches
		/// </summary>
		/// <param name="path">Path of root</param>
		/// <param name="includeInKey">set this to false if you want to ignore all files under this folder (you know they are _always_ the same for all machines)</param>
		/// <param name="id">Id of root. On windows these numbers need to increase two at the time since double backslash paths are added automatically under the hood</param>
		/// <returns>True if successful</returns>
		public abstract bool RegisterRoot(string path, bool includeInKey = true, byte id = 0);

		/// <summary>
		/// Register system roots used to normalize paths in caches
		/// </summary>
		/// <param name="startId">Start id for system roots. On windows these take up 10 entries</param>
		/// <returns>True if successful</returns>
		public abstract bool RegisterSystemRoots(byte startId = 0);

		/// <summary>
		/// Create root paths instance
		/// </summary>
		public static IRootPaths Create(ILogger logger)
		{
			return new RootPathsImpl(logger);
		}
	}

	/// <summary>
	/// Struct containing results from artifact fetch
	/// </summary>
	/// <param name="Success">Is set to true if succeeded in fetching artifacts</param>
	/// <param name="LogLines">Contains log lines if any</param>
	public readonly record struct FetchFromCacheResult(bool Success, List<string> LogLines);

	/// <summary>
	/// Base interface for a cache client
	/// </summary>
	public interface ICacheClient : IBaseInterface
	{
		/// <summary>
		/// Connect to cache client
		/// </summary>
		/// <param name="host">Cache server address</param>
		/// <param name="port">Cache server port</param>
		/// <returns>True if successful</returns>
		public abstract bool Connect(string host, int port);

		/// <summary>
		/// Write to cache
		/// </summary>
		/// <param name="rootPaths">RootPath instance</param>
		/// <param name="bucket">Bucket to store cache entry</param>
		/// <param name="process">Process</param>
		/// <param name="inputs">Input files</param>
		/// <param name="inputsSize">Input files size</param>
		/// <param name="outputs">Output files</param>
		/// <param name="outputsSize">Output files size</param>
		/// <returns>True if successful</returns>
		public abstract bool WriteToCache(IRootPaths rootPaths, uint bucket, IProcess process, byte[] inputs, uint inputsSize, byte[] outputs, uint outputsSize);

		/// <summary>
		/// Fetch from cache
		/// </summary>
		/// <param name="rootPaths">RootPath instance</param>
		/// <param name="bucket">Bucket to search for cache entry</param>
		/// <param name="info">Process start info</param>
		/// <returns>True if successful</returns>
		public abstract FetchFromCacheResult FetchFromCache(IRootPaths rootPaths, uint bucket, ProcessStartInfo info);

		/// <summary>
		/// Request the connected server to shutdown
		/// </summary>
		/// <param name="reason">Reason for shutdown</param>
		public abstract void RequestServerShutdown(string reason);

		/// <summary>
		/// Create a ICacheClient object
		/// </summary>
		/// <param name="session">The session</param>
		/// <param name="reportMissReason">Output reason for cache miss to log.</param>
		/// <param name="crypto">Enable crypto by using a 32 character crypto string (representing a 16 byte value)</param>
		/// <returns>The ICacheClient</returns>
		public static ICacheClient CreateCacheClient(ISessionServer session, bool reportMissReason, string crypto = "")
		{
			return new CacheClientImpl(session, reportMissReason, crypto);
		}
	}
}
