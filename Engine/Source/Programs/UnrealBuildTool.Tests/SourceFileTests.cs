// Copyright Epic Games, Inc. All Rights Reserved.

using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Linq;
using EpicGames.Core;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using OpenTracing.Util;
using UnrealBuildBase;
using UnrealBuildTool;

namespace UnrealBuildToolTests
{
	/// <summary>
	/// Tests for reading source file markup
	/// </summary>
	[TestClass]
	public class SourceFileTests
	{
		[TestMethod]
		public void Run()
		{
			UnrealHelper.InitializePath();

			List<DirectoryReference> baseDirectories = new List<DirectoryReference>();
			baseDirectories.Add(DirectoryReference.Combine(Unreal.EngineSourceDirectory, "Runtime"));
			baseDirectories.Add(DirectoryReference.Combine(Unreal.EngineSourceDirectory, "Developer"));
			baseDirectories.Add(DirectoryReference.Combine(Unreal.EngineSourceDirectory, "Editor"));

			foreach (FileReference pluginFile in PluginsBase.EnumeratePlugins((FileReference?)null))
			{
				DirectoryReference pluginSourceDir = DirectoryReference.Combine(pluginFile.Directory, "Source");
				if (DirectoryReference.Exists(pluginSourceDir))
				{
					baseDirectories.Add(pluginSourceDir);
				}
			}

			ConcurrentBag<SourceFile> sourceFiles = new ConcurrentBag<SourceFile>();
			using (GlobalTracer.Instance.BuildSpan("Scanning source files").StartActive())
			{
				using (ThreadPoolWorkQueue queue = new ThreadPoolWorkQueue())
				{
					foreach (DirectoryReference baseDirectory in baseDirectories)
					{
						queue.Enqueue(() => ParseSourceFiles(DirectoryItem.GetItemByDirectoryReference(baseDirectory), sourceFiles, queue));
					}
				}
			}

			FileReference tempDataFile = FileReference.Combine(Unreal.EngineDirectory, "Intermediate", "Temp", "SourceFileTests.bin");
			DirectoryReference.CreateDirectory(tempDataFile.Directory);

			using (GlobalTracer.Instance.BuildSpan("Writing source file data").StartActive())
			{
				using (BinaryArchiveWriter writer = new BinaryArchiveWriter(tempDataFile))
				{
					writer.WriteList(sourceFiles.ToList(), x => x.Write(writer));
				}
			}

			List<SourceFile>? readSourceFiles = new List<SourceFile>();
			using (GlobalTracer.Instance.BuildSpan("Reading source file data").StartActive())
			{
				using (BinaryArchiveReader reader = new BinaryArchiveReader(tempDataFile))
				{
					readSourceFiles = reader.ReadList(() => new SourceFile(reader));
				}
			}
		}

		static void ParseSourceFiles(DirectoryItem directory, ConcurrentBag<SourceFile> sourceFiles, ThreadPoolWorkQueue queue)
		{
			foreach (DirectoryItem subDirectory in directory.EnumerateDirectories())
			{
				queue.Enqueue(() => ParseSourceFiles(subDirectory, sourceFiles, queue));
			}

			foreach (FileItem file in directory.EnumerateFiles())
			{
				if (file.HasExtension(".h") || file.HasExtension(".cpp"))
				{
					queue.Enqueue(() => sourceFiles.Add(new SourceFile(file)));
				}
			}
		}
	}
}
