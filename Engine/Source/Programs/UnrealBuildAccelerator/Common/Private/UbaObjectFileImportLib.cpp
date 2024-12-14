// Copyright Epic Games, Inc. All Rights Reserved.

#include "UbaObjectFileImportLib.h"

namespace uba
{
	bool IsImportLib(const u8* data, u64 dataSize)
	{
		return dataSize > 5 && memcmp(data, "!<arch>", 5) == 0;
	}

	bool ObjectFileImportLib::Parse(Logger& logger, const tchar* hint)
	{
#if PLATFORM_WINDOWS

		u8* pos = m_data;
		pos += IMAGE_ARCHIVE_START_SIZE;
		//auto& header = *(IMAGE_ARCHIVE_MEMBER_HEADER*)pos;
		pos += sizeof(IMAGE_ARCHIVE_MEMBER_HEADER);
		
		u32 symbolCount = _byteswap_ulong(*(u32*)pos);
		pos += sizeof(u32);

		u32* symbolOffsets = (u32*)pos;
		pos += sizeof(u32)*symbolCount;

		Vector<char*> impSymbols;
		for (u32 i=0; i!=symbolCount; ++i)
		{
			u32 symbolOffset = _byteswap_ulong(symbolOffsets[i]);(void)symbolOffset;
			auto symbolStr = (char*)pos;
			std::string symbol(symbolStr);
			pos += symbol.size() + 1;
			if (i == 0)
			{
				m_libName = symbol.data() + strlen("__IMPORT_DESCRIPTOR_");
			}
			if (i < 3) // Skip the predefined symbols
				continue;
			if (strncmp(symbol.data(), "__imp_", 6) == 0)
			{
				impSymbols.push_back(symbolStr + 6);
				continue;
			}
			m_exports.try_emplace(std::move(symbol), ExportInfo{ ",DATA", i });
		}

		for (auto symbol : impSymbols)
		{
			auto findIt = m_exports.find(symbol);
			if (findIt != m_exports.end())
				findIt->second.extra = std::string();
		}

		return true;
#else
		return false;
#endif
	}

	const char* ObjectFileImportLib::GetLibName()
	{
		return m_libName.c_str();
	}

	bool ObjectFileImportLib::StripExports(Logger& logger, u8* newData, const UnorderedSymbols& allNeededImports)
	{
		return logger.Error(TC("Stripping exports from import lib file not supported"));
	}
}
