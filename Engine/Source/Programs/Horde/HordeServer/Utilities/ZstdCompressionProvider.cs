// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using Microsoft.AspNetCore.ResponseCompression;

namespace HordeServer.Utilities
{
	/// <summary>
	/// Compression provider for zstd responses
	/// </summary>
	class ZstdCompressionProvider : ICompressionProvider
	{
		/// <inheritdoc/>
		public string EncodingName => "zstd";

		/// <inheritdoc/>
		public bool SupportsFlush => true;

		/// <inheritdoc/>
		public Stream CreateStream(Stream outputStream)
		{
			return new ZstdSharp.CompressionStream(outputStream, leaveOpen: false);
		}
	}
}
