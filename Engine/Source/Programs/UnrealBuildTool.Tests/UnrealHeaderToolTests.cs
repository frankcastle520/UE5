// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.UHT.Utils;
using Microsoft.Extensions.Logging;
using UnrealBuildTool.Modes;
using Microsoft.VisualStudio.TestTools.UnitTesting;

namespace UnrealBuildToolTests
{
	[TestClass]
	public class UnrealHeaderToolTests
	{
		[TestMethod]
		[Ignore]
		public void Run()
		{
			UnrealHelper.InitializePath();

			string[] Arguments = System.Array.Empty<string>();
			CommandLineArguments CommandLineArguments = new CommandLineArguments(Arguments);
			UhtGlobalOptions Options = new UhtGlobalOptions(CommandLineArguments);

			// Initialize the attributes
			UhtTables Tables = new UhtTables();

			// Initialize the configuration
			IUhtConfig Config = new UhtConfigImpl(CommandLineArguments);

			// Run the tests
			using ILoggerFactory factory = LoggerFactory.Create(x => x.AddEpicDefault());
			Assert.IsTrue(UhtTestHarness.RunTests(Tables, Config, Options, factory.CreateLogger<UhtTestHarness>()));
		}
	}
}
