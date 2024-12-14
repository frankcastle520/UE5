// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.IO;
using System.Reflection;
using EpicGames.Core;
using UnrealBuildBase;

namespace UnrealBuildToolTests
{
	static class UnrealHelper
	{
		public static void InitializePath()
		{
			if (Unreal.LocationOverride.RootDirectory != null)
			{
				return;
			}

			// Use the EntryAssembly (the application path), rather than the ExecutingAssembly (the library path)
			string AssemblyLocation = Assembly.GetEntryAssembly()!.GetOriginalLocation();

			DirectoryReference? FoundRootDirectory = DirectoryReference.FindCorrectCase(DirectoryReference.FromString(AssemblyLocation)!);

			// Search up through the directory tree for the deepest instance of the sub-path "Engine/Source/Programs"
			while (FoundRootDirectory != null)
			{
				if (String.Equals("Programs", FoundRootDirectory.GetDirectoryName(), StringComparison.OrdinalIgnoreCase))
				{
					FoundRootDirectory = FoundRootDirectory.ParentDirectory;
					if (FoundRootDirectory != null && String.Equals("Source", FoundRootDirectory.GetDirectoryName(), StringComparison.OrdinalIgnoreCase))
					{
						FoundRootDirectory = FoundRootDirectory.ParentDirectory;
						if (FoundRootDirectory != null && String.Equals("Engine", FoundRootDirectory.GetDirectoryName(), StringComparison.OrdinalIgnoreCase))
						{
							FoundRootDirectory = FoundRootDirectory.ParentDirectory;
							break;
						}
						continue;
					}
					continue;
				}
				FoundRootDirectory = FoundRootDirectory.ParentDirectory;
			}

			if (FoundRootDirectory == null)
			{
				throw new Exception($"This code requires that applications using it are launched from a path containing \"Engine/Source/Programs\". This application was launched from {Path.GetDirectoryName(AssemblyLocation)}");
			}

			// Confirm that we've found a valid root directory, by testing for the existence of a well-known file
			FileReference ExpectedExistingFile = FileReference.Combine(FoundRootDirectory, "Engine", "Build", "Build.version");
			if (!FileReference.Exists(ExpectedExistingFile))
			{
				throw new Exception($"Expected file \"Engine/Build/Build.version\" was not found at {ExpectedExistingFile.FullName}");
			}

			Unreal.LocationOverride.RootDirectory = FoundRootDirectory;
		}
	}
}
