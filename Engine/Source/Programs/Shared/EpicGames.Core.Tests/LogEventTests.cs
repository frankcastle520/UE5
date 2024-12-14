// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using Microsoft.Extensions.Logging;
using Microsoft.VisualStudio.TestTools.UnitTesting;

namespace EpicGames.Core.Tests
{
	[TestClass]
	public class LogEventTests
	{
		class TestClass
		{
			public int Foo { get; set; }
			public string? Bar { get; set; }

			public override string ToString()
				=> $"TestClass({Foo},{Bar})";
		}

		[TestMethod]
		public void ArgumentTests()
		{
			CaptureLogger logger = new CaptureLogger();
			logger.LogInformation("Test {Value} {@Value}", new TestClass { Foo = 123, Bar = "456" }, new TestClass { Foo = 789, Bar = "hello" });

			LogEvent logEvent = logger.Events[0];
			logEvent.Time = new DateTime(2024, 1, 1);

			Assert.AreEqual("Test TestClass(123,456) {\"foo\":789,\"bar\":\"hello\"}", logEvent.ToString());
		}

		[TestMethod]
		public void RoundTripTests()
		{
			DateTime time = new DateTime(2024, 1, 1);
			LogEvent ev = new LogEvent(time, LogLevel.Information, default, "hello", null, new KeyValuePair<string, object?>[] { KeyValuePair.Create<string, object?>("foo", new LogValue(new Utf8String("type"), "text")) }, null);

			JsonLogEvent jev = JsonLogEvent.Parse(ev.ToJsonBytes());
			string jevs = jev.ToString();
			Assert.AreEqual("{\"time\":\"2024-01-01T00:00:00\",\"level\":\"Information\",\"message\":\"hello\",\"properties\":{\"foo\":{\"$type\":\"type\",\"$text\":\"text\"}}}", jevs);

			LogEvent ev2 = LogEvent.Read(jev.Data.Span);
			Assert.AreEqual(ev.ToString(), ev2.ToString());

			JsonLogEvent jev2 = JsonLogEvent.Parse(ev2.ToJsonBytes());
			string jevs2 = jev2.ToString();
			Assert.AreEqual(jevs, jevs2);
		}
	}
}
