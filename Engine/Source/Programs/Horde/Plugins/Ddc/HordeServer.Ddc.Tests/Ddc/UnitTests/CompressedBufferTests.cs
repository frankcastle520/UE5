// Copyright Epic Games, Inc. All Rights Reserved.

using System.Text;
using EpicGames.Core;
using HordeServer.Ddc;
using Microsoft.Extensions.Options;
using Moq;
using OpenTelemetry.Trace;

namespace HordeServer.Tests.Ddc.UnitTests
{
	[TestClass]
	public class CompressedBufferTests
	{

		[TestMethod]
		public async Task CompressAndDecompressAsync()
		{
			byte[] bytes = Encoding.UTF8.GetBytes("this is a test string");

			Tracer tracer = TracerProvider.Default.GetTracer("TestTracer");
			BufferedPayloadOptions bufferedPayloadOptions = new BufferedPayloadOptions()
			{
			};
			IOptionsMonitor<BufferedPayloadOptions> payloadOptionsMock = Mock.Of<IOptionsMonitor<BufferedPayloadOptions>>(_ => _.CurrentValue == bufferedPayloadOptions);
			BufferedPayloadFactory bufferedPayloadFactory = new BufferedPayloadFactory(payloadOptionsMock, tracer);
			CompressedBufferUtils bufferUtils = new(tracer, bufferedPayloadFactory);

			using MemoryStream ms = new MemoryStream();
			IoHash uncompressedHash = bufferUtils.CompressContent(ms, OoodleCompressorMethod.Mermaid, OoodleCompressionLevel.VeryFast, bytes);
			ms.Position = 0;

			using IBufferedPayload bufferedPayload = await bufferUtils.DecompressContentAsync(ms, (ulong)ms.Length, CancellationToken.None);
			await using Stream s = bufferedPayload.GetStream();
			byte[] roundTrippedBytes = await s.ReadAllBytesAsync();
			CollectionAssert.AreEqual(bytes, roundTrippedBytes);
		}
	}
}
