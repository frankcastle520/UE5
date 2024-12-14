// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.IO;
using EpicGames.Core;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using UnrealBuildTool;

#nullable enable

namespace UnrealBuildToolTests
{

	[TestClass]
	public class ConfigFileTests
	{
		#region -- Test Context --

		private const string TEST_FILE_NAME = "ConfigFileTest.ini";

		private class TestConfigFile : IDisposable
		{
			internal string? TemporaryFile { get; private set; }

			internal TestConfigFile(string FileName, string Contents)
			{
				try
				{
					TemporaryFile = Path.Combine(System.IO.Path.GetTempPath(), Guid.NewGuid().ToString() + "_" + FileName);
					using (StreamWriter writer = new StreamWriter(TemporaryFile))
					{
						writer.Write(Contents);
					}
				}
				catch (Exception)
				{
					TemporaryFile = null;
				}
			}

			public void Dispose()
			{
				if (!String.IsNullOrEmpty(TemporaryFile) && File.Exists(TemporaryFile))
				{
					File.Delete(TemporaryFile);
				}
			}
		}

		#endregion

		[TestMethod]
		public void ConfigFileTestsQuotations()
		{
			const string TestSection = "MySection";
			const string TestValueKey = "Name";
			const string TestValue = "\"https://www.unrealengine.com/\"";

			string TestContents = String.Format("[{0}]\r\n{1}={2}", TestSection, TestValueKey, TestValue);

			using (TestConfigFile TemporaryConfigFile = new TestConfigFile(TEST_FILE_NAME, TestContents))
			{
				string? TestFilePath = TemporaryConfigFile.TemporaryFile;

				Assert.IsFalse(String.IsNullOrEmpty(TestFilePath), "Unable to generate a test file.");

				ConfigFile ConfigFile;
				FileReference ConfigFileLocation = new FileReference(TestFilePath);

				Assert.IsTrue(FileReference.Exists(ConfigFileLocation), "Unable to acquire a test file.");
				
				ConfigFile = new ConfigFile(ConfigFileLocation);

				FileReference.MakeWriteable(ConfigFileLocation);
				ConfigFile.Write(ConfigFileLocation);

				Assert.IsNotNull(TemporaryConfigFile.TemporaryFile);
				string FileContents = File.ReadAllText(TemporaryConfigFile.TemporaryFile);
				Assert.AreEqual(TestContents.Trim(), FileContents.Trim(), "The file contents after writing do not match the expected contents.");
			}
		}
	}
}
