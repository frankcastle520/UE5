// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Horde.Jobs.Templates;
using EpicGames.Horde.Streams;
using HordeServer.Streams;

namespace HordeServer.Tests.Streams
{
	[TestClass]
	public class StreamCollectionTests : BuildTestSetup
	{
		private readonly StreamId _streamId = new("bogusStreamId");

		[TestMethod]
		public void ValidateUndefinedTemplateIdInTabs()
		{
			StreamConfig config = new()
			{
				Tabs = new()
				{
					new TabConfig { Templates = new List<TemplateId> { new ("foo") }},
					new TabConfig { Templates = new List<TemplateId> { new ("bar") }}
				},
				Templates = new() { new TemplateRefConfig { Id = new TemplateId("foo") } }
			};

			Assert.ThrowsException<InvalidStreamException>(() => HordeServer.Streams.StreamCollection.Validate(_streamId, config));

			config.Templates.Add(new TemplateRefConfig { Id = new TemplateId("bar") });
			HordeServer.Streams.StreamCollection.Validate(_streamId, config);
		}
	}
}
