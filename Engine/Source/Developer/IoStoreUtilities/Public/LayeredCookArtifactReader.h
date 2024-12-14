// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"
#include "CookArtifactReaderCommon.h"
#include "Templates/SharedPointer.h"

class FLayeredCookArtifactReader
	: public FCookArtifactReaderCommon
{
public:
	IOSTOREUTILITIES_API FLayeredCookArtifactReader() = default;

	IOSTOREUTILITIES_API void AddLayer(TSharedRef<ICookArtifactReader> InLayer);
	IOSTOREUTILITIES_API bool RemoveLayer(TSharedRef<ICookArtifactReader> InLayer);
	IOSTOREUTILITIES_API void EmptyLayers();

	IOSTOREUTILITIES_API bool FileExists(const TCHAR* Filename) override;
	IOSTOREUTILITIES_API int64 FileSize(const TCHAR* Filename) override;
	IOSTOREUTILITIES_API IFileHandle* OpenRead(const TCHAR* Filename) override;

	IOSTOREUTILITIES_API bool IterateDirectory(const TCHAR* Directory, IPlatformFile::FDirectoryVisitor& Visitor) override;
private:
	TArray<TSharedRef<ICookArtifactReader>> Layers;
};
