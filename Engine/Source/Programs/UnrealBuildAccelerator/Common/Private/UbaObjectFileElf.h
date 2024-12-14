// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UbaObjectFile.h"

namespace uba
{
	class ObjectFileElf : public ObjectFile
	{
	public:
		ObjectFileElf();
		virtual bool Parse(Logger& logger, const tchar* hint) override;

		static bool CreateExtraFile(Logger& logger, const StringView& platform, MemoryBlock& memoryBlock, const UnorderedSymbols& allExternalImports, const UnorderedSymbols& allInternalImports, const UnorderedExports& allExports, bool includeExportsInFile);

	private:
		virtual bool StripExports(Logger& logger, u8* newData, const UnorderedSymbols& allExternalImports) override;

		u64 m_symTableNamesOffset = 0;
		u64 m_dynTableNamesOffset = 0;
		bool m_useVisibilityForExports = true;
	};

	bool IsElfFile(const u8* data, u64 dataSize);
}
