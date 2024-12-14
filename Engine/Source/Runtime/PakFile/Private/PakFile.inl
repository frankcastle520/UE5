// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

/**
 * Search the given FDirectoryIndex for all files under the given Directory.  Helper for FindFilesAtPath, called separately on the DirectoryIndex or Pruned DirectoryIndex. Does not use
 * FScopedPakDirectoryIndexAccess internally; caller is responsible for calling from within a lock.
 * Returned paths are full paths (include the mount point)
 */
template <typename ShouldVisitFunc, class ContainerType>
void FPakFile::FindFilesAtPathInIndex(const FDirectoryIndex& TargetIndex, ContainerType& OutFiles,
	const FString& Directory, const ShouldVisitFunc& ShouldVisit, bool bIncludeFiles, bool bIncludeDirectories,
	bool bRecursive) const
{
	FStringView RelativeSearch;
	if (Directory.StartsWith(MountPoint))
	{
		RelativeSearch = FStringView(Directory).RightChop(MountPoint.Len());
	}
	else
	{
		// Directory is unnormalized and might not end with /; MountPoint is guaranteed to end with /.
		// Act as if we were called with a normalized directory if adding slash makes it match MountPoint.
		if (FStringView(Directory).StartsWith(FStringView(MountPoint).LeftChop(1)))
		{
			RelativeSearch = FStringView();
		}
		else
		{
			// Early out; directory does not start with MountPoint and so will not match any of files in this pakfile.
			return;
		}
	}

	TArray<FString> DirectoriesInPak; // List of all unique directories at path
	for (TMap<FString, FPakDirectory>::TConstIterator It(TargetIndex); It; ++It)
	{
		// Check if the file is under the specified path.
		if (RelativeSearch.IsEmpty() || FStringView(It.Key()).StartsWith(RelativeSearch))
		{
			FString PakPath = PakPathCombine(MountPoint, It.Key());
			if (bRecursive == true)
			{
				// Add everything
				if (bIncludeFiles)
				{
					for (FPakDirectory::TConstIterator DirectoryIt(It.Value()); DirectoryIt; ++DirectoryIt)
					{
						const FString& FilePathUnderDirectory = DirectoryIt.Key();
						if (ShouldVisit(FilePathUnderDirectory))
						{
							OutFiles.Add(PakPathCombine(PakPath, FilePathUnderDirectory));
						}
					}
				}
				if (bIncludeDirectories)
				{
					if (Directory != PakPath)
					{
						if (ShouldVisit(PakPath))
						{
							DirectoriesInPak.Add(MoveTemp(PakPath));
						}
					}
				}
			}
			else
			{
				int32 SubDirIndex = PakPath.Len() > Directory.Len() ? PakPath.Find(TEXT("/"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Directory.Len() + 1) : INDEX_NONE;
				// Add files in the specified folder only.
				if (bIncludeFiles && SubDirIndex == INDEX_NONE)
				{
					for (FPakDirectory::TConstIterator DirectoryIt(It.Value()); DirectoryIt; ++DirectoryIt)
					{
						const FString& FilePathUnderDirectory = DirectoryIt.Key();
						if (ShouldVisit(FilePathUnderDirectory))
						{
							OutFiles.Add(PakPathCombine(PakPath, FilePathUnderDirectory));
						}
					}
				}
				// Add sub-folders in the specified folder only
				if (bIncludeDirectories && SubDirIndex >= 0)
				{
					FString SubDirPath = PakPath.Left(SubDirIndex + 1);
					if (ShouldVisit(SubDirPath))
					{
						DirectoriesInPak.AddUnique(MoveTemp(SubDirPath));
					}
				}
			}
		}
	}
	OutFiles.Append(MoveTemp(DirectoriesInPak));
}

template <typename ShouldVisitFunc, class ContainerType>
void FPakFile::FindPrunedFilesAtPathInternal(const TCHAR* InPath, const ShouldVisitFunc& ShouldVisit, ContainerType& OutFiles,
	bool bIncludeFiles, bool bIncludeDirectories, bool bRecursive) const
{
	// Make sure all directory names end with '/'.
	FString Directory(InPath);
	MakeDirectoryFromPath(Directory);

	// Check the specified path is under the mount point of this pak file.
	// The reverse case (MountPoint StartsWith Directory) is needed to properly handle
	// pak files that are a subdirectory of the actual directory.
	if (!Directory.StartsWith(MountPoint) && !MountPoint.StartsWith(Directory))
	{
		return;
	}

	FScopedPakDirectoryIndexAccess ScopeAccess(*this);
#if ENABLE_PAKFILE_RUNTIME_PRUNING_VALIDATE
	if (ShouldValidatePrunedDirectory())
	{
		TSet<FString> FullFoundFiles, PrunedFoundFiles;
		FindFilesAtPathInIndex(DirectoryIndex, FullFoundFiles, Directory, ShouldVisit,
			bIncludeFiles, bIncludeDirectories, bRecursive);
		FindFilesAtPathInIndex(PrunedDirectoryIndex, PrunedFoundFiles, Directory, ShouldVisit,
			bIncludeFiles, bIncludeDirectories, bRecursive);
		ValidateDirectorySearch(FullFoundFiles, PrunedFoundFiles, InPath);

		for (const FString& FoundFile : FullFoundFiles)
		{
			OutFiles.Add(FoundFile);
		}
	}
	else
#endif
	{
		FindFilesAtPathInIndex(DirectoryIndex, OutFiles, Directory, ShouldVisit,
			bIncludeFiles, bIncludeDirectories, bRecursive);
	}
}
