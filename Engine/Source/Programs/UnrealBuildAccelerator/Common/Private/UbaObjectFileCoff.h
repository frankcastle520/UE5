// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UbaObjectFile.h"

namespace uba
{

	class ObjectFileCoff : public ObjectFile
	{
	public:
		ObjectFileCoff();
		virtual bool Parse(Logger& logger, const tchar* hint) override;

		static bool CreateExtraFile(Logger& logger, const StringView& platform, MemoryBlock& memoryBlock, const UnorderedSymbols& allExternalImports, const UnorderedSymbols& allInternalImports, const UnorderedExports& allSharedExports, bool includeExportsInFile);

	private:
		struct Info;

		bool ParseExports();
		template<typename SymbolType> void ParseImports();

		virtual bool StripExports(Logger& logger, u8* newData, const UnorderedSymbols& allExternalImports) override;

		template<typename SymbolType> void CalculateImports(Logger& logger, Vector<u32>& outImports);
		template<typename SymbolType> void WriteImports(Logger& logger, u8* newData, Info& newInfo, const Vector<u32>& symbolsToAdd);
		template<typename SymbolType> void RemoveSymbols(Logger& logger, u8* newData, Info& newInfo);

		struct Info
		{
			u32 sectionsMemOffset = 0;
			u32 sectionCount = 0;
			u64 directiveSectionMemOffset = 0;
			u32 stringTableMemPos = 0;
			u32 symbolsMemPos = 0;
			u32 symbolCount = 0;
		};

		bool m_isBigObj = false;
		Info m_info;

		UnorderedSymbols m_loopbacksToAdd;
		UnorderedSymbols m_toRemove;

		static UnorderedSymbols PotentiallyDuplicatedSymbols;
		static UnorderedSymbols ExportsToKeep;
	};

	bool IsCoffFile(const u8* data, u64 dataSize);
}
