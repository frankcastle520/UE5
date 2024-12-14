// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"
#include "Containers/UnrealString.h"
#include "GenericPlatform/GenericPlatformFile.h"

class FArchive;

class ICookArtifactReader
{
public:
	virtual ~ICookArtifactReader() = default;
	
	virtual bool FileExists(const TCHAR* Filename) = 0;
	virtual int64 FileSize(const TCHAR* Filename) = 0;
	virtual IFileHandle* OpenRead(const TCHAR* Filename) = 0;

	virtual bool IterateDirectory(const TCHAR* Directory, IPlatformFile::FDirectoryVisitor& Visitor) = 0;

	// Utilities
	virtual FArchive* CreateFileReader(const TCHAR* Filename) = 0;
	virtual bool IterateDirectoryRecursively(const TCHAR* Directory, IPlatformFile::FDirectoryVisitor& Visitor) = 0;
	virtual void FindFiles(TArray<FString>& Result, const TCHAR* Filename, bool Files, bool Directories) = 0;
	virtual void FindFiles(TArray<FString>& FoundFiles, const TCHAR* Directory, const TCHAR* FileExtension = nullptr) = 0;
};
