// Copyright Epic Games, Inc. All Rights Reserved.

using System.Globalization;
using HordeServer.Streams;

namespace HordeServer.Tests.Streams
{
	[TestClass]
	public class StreamServiceTests : BuildTestSetup
	{
		[TestMethod]
		public async Task PausingAsync()
		{
			Fixture fixture = await CreateFixtureAsync();

			IStream stream = (await StreamCollection.GetAsync(fixture!.StreamConfig!))!;
			Assert.IsFalse(stream.IsPaused(DateTime.UtcNow));
			Assert.IsNull(stream.PausedUntil);
			Assert.IsNull(stream.PauseComment);

			DateTime pausedUntil = DateTime.UtcNow.AddHours(1);
			await StreamCollection.UpdatePauseStateAsync(stream, newPausedUntil: pausedUntil, newPauseComment: "mycomment");
			stream = (await StreamCollection.GetAsync(fixture!.StreamConfig!))!;
			// Comparing by string to avoid comparing exact milliseconds as those are not persisted in MongoDB fields
			Assert.IsTrue(stream.IsPaused(DateTime.UtcNow));
			Assert.AreEqual(pausedUntil.ToString(CultureInfo.InvariantCulture), stream.PausedUntil!.Value.ToString(CultureInfo.InvariantCulture));
			Assert.AreEqual("mycomment", stream.PauseComment);

			await StreamCollection.UpdatePauseStateAsync(stream, newPausedUntil: null, newPauseComment: null);
			stream = (await StreamCollection.GetAsync(fixture!.StreamConfig!))!;
			Assert.IsFalse(stream.IsPaused(DateTime.UtcNow));
			Assert.IsNull(stream.PausedUntil);
			Assert.IsNull(stream.PauseComment);
		}
	}
}
