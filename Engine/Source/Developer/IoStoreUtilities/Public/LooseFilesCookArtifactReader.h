// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CookArtifactReaderCommon.h"

class FLooseFilesCookArtifactReader
	: public FCookArtifactReaderCommon
{
public:
	IOSTOREUTILITIES_API FLooseFilesCookArtifactReader() = default;

	IOSTOREUTILITIES_API bool FileExists(const TCHAR* Filename) override;
	IOSTOREUTILITIES_API int64 FileSize(const TCHAR* Filename) override;
	IOSTOREUTILITIES_API IFileHandle* OpenRead(const TCHAR* Filename) override;

	IOSTOREUTILITIES_API bool IterateDirectory(const TCHAR* Directory, IPlatformFile::FDirectoryVisitor& Visitor) override;
};
