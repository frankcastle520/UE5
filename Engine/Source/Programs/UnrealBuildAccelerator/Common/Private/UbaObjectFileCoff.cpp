// Copyright Epic Games, Inc. All Rights Reserved.

#include "UbaObjectFileCoff.h"
#include "UbaFileAccessor.h"

namespace uba
{
	constexpr u16 ImageFileMachineUnknown = 0;
	constexpr u8 ImageSizeofShortName = 8; // IMAGE_SIZEOF_SHORT_NAME
	constexpr u16 ImageSymClassExternal = 0x0002; // IMAGE_SYM_CLASS_EXTERNAL
	constexpr u16 ImageSymUndefined = 0; // IMAGE_SYM_UNDEFINED

	constexpr u32 ImageScnCntCode = 0x00000020; // IMAGE_SCN_CNT_CODE
	constexpr u32 ImageScnLnkInfo = 0x00000200; // IMAGE_SCN_LNK_INFO
	constexpr u32 ImageScnLnkRemove = 0x00000800; // IMAGE_SCN_LNK_REMOVE
	constexpr u32 ImageScnLnkComdat = 0x00001000; // IMAGE_SCN_LNK_COMDAT
	constexpr u32 ImageScnAlign1Bytes = 0x00100000; // IMAGE_SCN_ALIGN_1BYTES
	constexpr u32 ImageScnMemExecute = 0x20000000; // IMAGE_SCN_MEM_EXECUTE
	constexpr u32 ImageScnMemRead = 0x40000000; // IMAGE_SCN_MEM_READ

	const u16 ImageRelAmd64Addr64 = 0x0001; // IMAGE_REL_AMD64_ADDR64


	#pragma pack(push)
	#pragma pack(1)

	struct ImageFileHeader // IMAGE_FILE_HEADER
	{
		u16 Machine;
		u16 NumberOfSections;
		u32 TimeDateStamp;
		u32 PointerToSymbolTable;
		u32 NumberOfSymbols;
		u16 SizeOfOptionalHeader;
		u16 Characteristics;
	};

	struct AnonObjectHeaderBigobj // ANON_OBJECT_HEADER_BIGOBJ
	{
		u16 Sig1;            // Must be IMAGE_FILE_MACHINE_UNKNOWN
		u16 Sig2;            // Must be 0xffff
		u16 Version;         // >= 2 (implies the Flags field is present)
		u16 Machine;         // Actual machine - IMAGE_FILE_MACHINE_xxx
		u32 TimeDateStamp;
		Guid ClassID;         // {D1BAA1C7-BAEE-4ba9-AF20-FAF66AA4DCB8}
		u32 SizeOfData;      // Size of data that follows the header
		u32 Flags;           // 0x1 -> contains metadata
		u32 MetaDataSize;    // Size of CLR metadata
		u32 MetaDataOffset;  // Offset of CLR metadata

		// bigobj specifics
		u32 NumberOfSections; // extended from WORD
		u32 PointerToSymbolTable;
		u32 NumberOfSymbols;
	};

	struct ImageSectionHeader // IMAGE_SECTION_HEADER
	{
		u8 Name[ImageSizeofShortName];
		union
		{
			u32 PhysicalAddress;
			u32 VirtualSize;
		} Misc;
		u32 VirtualAddress;
		u32 SizeOfRawData;
		u32 PointerToRawData;
		u32 PointerToRelocations;
		u32 PointerToLinenumbers;
		u16 NumberOfRelocations;
		u16 NumberOfLinenumbers;
		u32 Characteristics;
	};
	static_assert(sizeof(ImageSectionHeader) == 40);

	struct ImageRelocation // IMAGE_RELOCATION
	{
		union
		{
			u32 VirtualAddress;
			u32 RelocCount; // Set to the real count when IMAGE_SCN_LNK_NRELOC_OVFL is set
		};
		u32 SymbolTableIndex;
		u16 Type;
	};


	struct ImageSymbolEx // IMAGE_SYMBOL_EX
	{
		union
		{
			char ShortName[8];
			struct
			{
				u32 Short; // if 0, use LongName
				u32 Long; // offset into string table
			} Name;
			u32 LongName[2]; // PBYTE  [2]
		} N;
		u32 Value;
		u32 SectionNumber;
		u16 Type;
		u8 StorageClass;
		u8 NumberOfAuxSymbols;
	};

	struct ImageSymbol // IMAGE_SYMBOL
	{
		union
		{
			u8 ShortName[8];
			struct
			{
				u32   Short; // if 0, use LongName
				u32   Long; // offset into string table
			} Name;
			u32   LongName[2]; // PBYTE [2]
		} N;
		u32 Value;
		u16 SectionNumber;
		u16 Type;
		u8 StorageClass;
		u8 NumberOfAuxSymbols;
	};

	#pragma pack(pop)

	// These are symbols that are added to all dlls through some macros.
	// When merging dlls we need to remove duplicates of these
	UnorderedSymbols ObjectFileCoff::PotentiallyDuplicatedSymbols = 
	{
#if 0 // This is not needed anymore
		// REPLACEMENT_OPERATOR_NEW_AND_DELETE
		"??2@YAPEAX_K@Z",
		"??2@YAPEAX_KAEBUnothrow_t@std@@@Z",
		"??2@YAPEAX_KW4align_val_t@std@@@Z",
		"??2@YAPEAX_KW4align_val_t@std@@AEBUnothrow_t@1@@Z",
		"??3@YAXPEAX@Z",
		"??3@YAXPEAXAEBUnothrow_t@std@@@Z",
		"??3@YAXPEAXW4align_val_t@std@@@Z",
		"??3@YAXPEAXW4align_val_t@std@@AEBUnothrow_t@1@@Z",
		"??3@YAXPEAX_K@Z",
		"??3@YAXPEAX_KAEBUnothrow_t@std@@@Z",
		"??3@YAXPEAX_KW4align_val_t@std@@@Z",
		"??3@YAXPEAX_KW4align_val_t@std@@AEBUnothrow_t@1@@Z",
		"??_U@YAPEAX_K@Z",
		"??_U@YAPEAX_KAEBUnothrow_t@std@@@Z",
		"??_U@YAPEAX_KW4align_val_t@std@@@Z",
		"??_U@YAPEAX_KW4align_val_t@std@@AEBUnothrow_t@1@@Z",
		"??_V@YAXPEAX@Z",
		"??_V@YAXPEAXAEBUnothrow_t@std@@@Z",
		"??_V@YAXPEAXW4align_val_t@std@@@Z",
		"??_V@YAXPEAXW4align_val_t@std@@AEBUnothrow_t@1@@Z",
		"??_V@YAXPEAX_K@Z",
		"??_V@YAXPEAX_KAEBUnothrow_t@std@@@Z",
		"??_V@YAXPEAX_KW4align_val_t@std@@@Z",
		"??_V@YAXPEAX_KW4align_val_t@std@@AEBUnothrow_t@1@@Z",

		// UE_DEFINE_FMEMORY_WRAPPERS
		"?FMemory_Free@@YAXPEAX@Z",
		"?FMemory_Malloc@@YAPEAX_K0@Z",
		"?FMemory_Realloc@@YAPEAXPEAX_K1@Z",

		// UE4_VISUALIZERS_HELPERS
		"?GNameBlocksDebug@@3PEAPEAEEA",
		"?GObjectIndexToPackedObjectRefDebug@@3AEAPEB_KEA",
		"?GObjectArrayForDebugVisualizers@@3AEAPEAVFChunkedFixedUObjectArray@@EA",
		"?GComplexObjectPathDebug@@3AEAPEAUFStoredObjectPathDebug@Private@CoreUObject@UE@@EA",
		"?GObjectHandlePackageDebug@@3AEAPEAUFObjectHandlePackageDebugData@Private@CoreUObject@UE@@EA",

		// Have no idea why these are duplicated
		"??_GIFunction_OwnedObject@Function@Private@Core@UE@@UEAAPEAXI@Z",
		"??_GIDelegateInstance@@UEAAPEAXI@Z",
#endif
	};

	bool IsBigObj(const u8* data, u64 size)
	{
		if (size < sizeof(AnonObjectHeaderBigobj))
			return false;
		auto& header = *(AnonObjectHeaderBigobj *)data;
		if (header.Sig1 != ImageFileMachineUnknown)
			return false;
		if (header.Sig2 != 0xffff)
			return false;
		if (header.Version < 2)
			return false;
		constexpr u8 bigObjClassId[16] = { 0xc7, 0xa1, 0xba, 0xd1, 0xee, 0xba, 0xa9, 0x4b, 0xaf, 0x20, 0xfa, 0xf6, 0x6a, 0xa4, 0xdc, 0xb8 };
		if (header.ClassID != *(const Guid*)bigObjClassId)
			return false;
		return true;
	}

	bool IsCoffFile(const u8* data, u64 dataSize)
	{
		if (IsBigObj(data, dataSize))
			return true;
		if (dataSize < sizeof(ImageFileHeader) + 8)
			return false;

		// TODO: This is not a solid way to identify a coff file.. don't know how this should be done
		auto header = *(ImageFileHeader*)data;
		if (header.Machine != 0x8664) // TODO: Add whatever other machines supported (ARM)
			return false;
		if (header.SizeOfOptionalHeader != 0) // Should always be 0
			return false;
		if (header.Characteristics != 0) // Should always be 0
			return false;
		// We expect .text or .drectve sections first.. this is also hacky.. but don't know how to verify that a file is a coff file
		const u8* firstSection = data + sizeof(ImageFileHeader);
		return memcmp(firstSection, ".text", 5) == 0 || memcmp(firstSection, ".drectve", 8) == 0;
	}

	template<typename SymbolType>
	ObjectFile::AnsiStringView GetSymbolName(SymbolType& symbol, const u8* data, u32 stringTableMemPos)
	{
		if (symbol.N.Name.Short == 0)
		{
			auto name = (const char*)(data + stringTableMemPos + symbol.N.Name.Long);
			return { name, name + strlen(name) };
		}
		auto shortName = (char*)symbol.N.ShortName;
		return { shortName, shortName + strnlen(shortName, ImageSizeofShortName) };
	}

	ObjectFileCoff::ObjectFileCoff()
	{
		m_type = ObjectFileType_Coff;
	}

	bool ObjectFileCoff::Parse(Logger& logger, const tchar* hint)
	{
		m_isBigObj = IsBigObj(m_data, m_dataSize);
		
		if (m_isBigObj)
		{
			auto& header = *(AnonObjectHeaderBigobj*)m_data;
			m_info.symbolsMemPos = header.PointerToSymbolTable;
			m_info.symbolCount = header.NumberOfSymbols;
			m_info.stringTableMemPos = header.PointerToSymbolTable + header.NumberOfSymbols * sizeof(ImageSymbolEx);
			m_info.sectionsMemOffset = sizeof(AnonObjectHeaderBigobj);
			m_info.sectionCount = header.NumberOfSections;
		}
		else
		{
			auto& header = *(ImageFileHeader*)m_data;
			m_info.symbolsMemPos = header.PointerToSymbolTable;
			m_info.symbolCount = header.NumberOfSymbols;
			m_info.stringTableMemPos = header.PointerToSymbolTable + header.NumberOfSymbols * sizeof(ImageSymbol);
			m_info.sectionsMemOffset = sizeof(ImageFileHeader);
			m_info.sectionCount = header.NumberOfSections;
		}

		if (!ParseExports())
			return false;

		if (m_isBigObj)
			ParseImports<ImageSymbolEx>();
		else
			ParseImports<ImageSymbol>();

		return true;
	}

	bool ObjectFileCoff::ParseExports()
	{
		auto sections = (ImageSectionHeader*)(m_data + m_info.sectionsMemOffset);
		for (u32 i=0; i!=m_info.sectionCount; ++i)
		{
			if (strncmp((char*)sections[i].Name, ".drectve", 8) != 0)
				continue;
			m_info.directiveSectionMemOffset = u64((u8*)(sections + i) - m_data);
			break;
		}
		if (!m_info.directiveSectionMemOffset)
			return true;

		auto directiveSection = (ImageSectionHeader*)(m_data + m_info.directiveSectionMemOffset);
		u8* directiveData = m_data + directiveSection->PointerToRawData;
		u8* directiveEnd = directiveData + directiveSection->SizeOfRawData;

		static constexpr u8 utf8Bom[3] = { 0xef, 0xbb, 0xbf };
		UBA_ASSERT(memcmp(directiveData, utf8Bom, 3) != 0);

		std::string tmp;
		std::string extra;

		u32 index = 0;

		auto str = (char*)directiveData;
		auto strEnd = (char*)directiveEnd;
		while (str)
		{
			char* exportStr = strstr(str, "/EXPORT:");
			if (!exportStr)
				break;
			exportStr += 8;
			char* exportEnd;
			if (*exportStr == '\"')
			{
				++exportStr;
				exportEnd = strchr(exportStr, '\"');
				str = exportEnd + 1;
			}
			else
			{
				exportEnd = (char*)memchr(exportStr, ' ', strEnd - exportStr);
				str = exportEnd;
				if (!exportEnd)
					exportEnd = strEnd;
				else
					++str;
			}

			tmp.assign(exportStr, exportEnd);
			extra.clear();
			if (const char* comma = strchr(tmp.c_str(), ','))
			{
				extra = comma;
				tmp = tmp.substr(0, comma - tmp.data());
			}
					
			m_exports.emplace(tmp, ExportInfo{extra, index++});
		}
		return true;
	}

	template<typename SymbolType>
	void ObjectFileCoff::ParseImports()
	{
		std::string symbolString;
		auto symbols = (SymbolType*)(m_data + m_info.symbolsMemPos);
		for (u32 i=0; i!=m_info.symbolCount; ++i)
		{
			auto& symbol = symbols[i];

			if (symbol.StorageClass != ImageSymClassExternal)
				continue;

			AnsiStringView symbolName = GetSymbolName(symbol, m_data, m_info.stringTableMemPos);
			symbolName.ToString(symbolString);

			if (symbol.SectionNumber != ImageSymUndefined)
			{
				if (PotentiallyDuplicatedSymbols.find(symbolString) != PotentiallyDuplicatedSymbols.end())
				{
					m_potentialDuplicates.emplace(symbolString);
				}
			}
			else
			{
				m_imports.emplace(symbolString);
			}
		}
	}

	bool ObjectFileCoff::StripExports(Logger& logger, u8* newData, const UnorderedSymbols& allExternalImports)
	{
		if (!m_info.directiveSectionMemOffset)
			return true;

		auto directiveSection = (const ImageSectionHeader*)(m_data + m_info.directiveSectionMemOffset);
		if (directiveSection->SizeOfRawData < 10)
			return true;

		const u8* directiveData = m_data + directiveSection->PointerToRawData;

		auto newDirectiveSection = (ImageSectionHeader*)(newData + m_info.directiveSectionMemOffset);
		u8* newDirectiveData = newData + newDirectiveSection->PointerToRawData;

		std::string tmp;

		char* writePos = (char*)newDirectiveData;
		const char* lastCopyPos = (const char*)directiveData;

		auto readPos = lastCopyPos;
		auto readEnd = readPos + directiveSection->SizeOfRawData;
		auto readLastPossiblePos = readEnd - 9;

		while (true)
		{
			const char* exportStr = nullptr;
			const char* it = readPos;

			while (it < readLastPossiblePos)
			{
				if (memcmp(it, "/EXPORT:", 8) != 0)
				{
					++it;
					continue;
				}

				exportStr = it;
				break;
			}

			if (!exportStr)
			{
				readPos = readEnd;
				break;
			}

			const char* startPos = exportStr;
			exportStr += 8;
			const char* exportEnd;
			if (*exportStr == '\"')
			{
				++exportStr;
				exportEnd = strchr(exportStr, '\"');
				readPos = exportEnd + 1;
				if (strncmp(readPos, ",DATA", 5) == 0)
					readPos += 5;
				readPos = Min(readPos, readEnd);
			}
			else
			{
				exportEnd = strchr(exportStr, ' ');
				if (!exportEnd)
					readPos = exportEnd = exportStr + strlen(exportStr);
				else
					readPos = exportEnd;
				if (strncmp(exportEnd-5, ",DATA", 5) == 0)
					exportEnd -= 5;
			}

			tmp.assign(exportStr, exportEnd - exportStr);
			if (allExternalImports.find(tmp) != allExternalImports.end())
				continue;
			tmp.assign("__imp_").append(exportStr, exportEnd - exportStr);
			if (allExternalImports.find(tmp) != allExternalImports.end())
				continue;
			
			u64 toCopy = startPos - lastCopyPos - 1;
			memcpy(writePos, lastCopyPos, toCopy);
			writePos += toCopy;
			lastCopyPos = readPos;
			if (!*readPos)
				break;
		}

		u64 toCopy = readPos - lastCopyPos;
		memcpy(writePos, lastCopyPos, toCopy);
		writePos += toCopy;

		u32 sizeOfRawData = directiveSection->SizeOfRawData;
		newDirectiveSection->SizeOfRawData = u32((u8*)writePos - newDirectiveData);
		UBA_ASSERT(newDirectiveSection->SizeOfRawData <= sizeOfRawData);

		memset(writePos, 0, sizeOfRawData - newDirectiveSection->SizeOfRawData);

		return true;
	}

	bool ObjectFileCoff::CreateExtraFile(Logger& logger, const StringView& platform, MemoryBlock& memoryBlock, const UnorderedSymbols& allExternalImports, const UnorderedSymbols& allInternalImports, const UnorderedExports& allExports, bool includeExportsInFile)
	{
		std::string tmp;

		u32 totalStringSize = 0;
		UnorderedSymbols neededLoopbacks;
		for (auto& symbol : allInternalImports)
		{
			if (strncmp(symbol.data(), "__imp_", 6) != 0)
				continue;
			tmp = symbol.substr(6);
			if (allExports.find(tmp) == allExports.end())
				continue;
			if (neededLoopbacks.insert(symbol).second)
				totalStringSize += u32(symbol.size()) + 1;
		}
		u32 loopbackCount = u32(neededLoopbacks.size());
		UBA_ASSERT(loopbackCount < 65536);


		auto allocate = [&](u64 size) { return memoryBlock.Allocate(size, 1, TC("")); };
		auto write = [&](const void* data, u64 size) { memcpy(allocate(size), data, size); };


		// Header
		auto& header = *(ImageFileHeader*)allocate(sizeof(ImageFileHeader));
		header.Machine = 0x8664;

		// Session for loopbacks
		auto& textSection = *(ImageSectionHeader*)allocate(sizeof(ImageSectionHeader));
		memcpy(textSection.Name, ".text$mn", 8);
		textSection.Characteristics = ImageScnCntCode | ImageScnMemExecute | ImageScnMemRead;
		u16 textSessionIndex = header.NumberOfSections;
		++header.NumberOfSections;

		// Directive section
		if (includeExportsInFile)
		{
			auto& directiveSection = *(ImageSectionHeader*)allocate(sizeof(ImageSectionHeader));
			memcpy(directiveSection.Name, ".drectve", 8);
			directiveSection.Characteristics = ImageScnAlign1Bytes|ImageScnLnkInfo|ImageScnLnkRemove;
			++header.NumberOfSections;

			auto writeExport = [&](const std::string& symbol, const std::string& extra)
				{
					write("/EXPORT:", 8);
					write(symbol.data(), symbol.size());
					write(extra.data(), extra.size());
					write(" ", 1);
				};

			// Directive raw data
			u32 directiveRawDataStart = u32(memoryBlock.writtenSize);
			directiveSection.PointerToRawData = directiveRawDataStart;
			for (auto& kv : allExports)
			{
				auto& symbol = kv.first;

				if (allExternalImports.find(symbol) == allExternalImports.end())
				{
					tmp.assign("__imp_").append(symbol);
					if (allExternalImports.find(tmp) == allExternalImports.end())
						continue;
				}

				writeExport(symbol, kv.second.extra);
			}

			tmp = "ThisIsAnUnrealEngineModule";
			if (allExports.find(tmp) != allExports.end())
				writeExport(tmp, ""); // Workaround for tool not liking empty lists

			write("", 1);
			directiveSection.SizeOfRawData = u32(memoryBlock.writtenSize) - directiveRawDataStart;
		}


		// Memory for relocations and write relocations
		u32 relocationsRawDataPos = u32(memoryBlock.writtenSize);
		allocate(loopbackCount * 8);
		u32 relocationsMemPos = u32(memoryBlock.writtenSize);
		auto relocations = (ImageRelocation*)allocate(loopbackCount * sizeof(ImageRelocation));
		textSection.PointerToRelocations = relocationsMemPos;
		textSection.NumberOfRelocations = u16(loopbackCount);
		textSection.PointerToRawData = relocationsRawDataPos;
		textSection.SizeOfRawData = loopbackCount * 8;
		for (u32 i=0; i!=loopbackCount; ++i)
		{
			auto& relocation = relocations[i];
			relocation.VirtualAddress = 8 * i;
			relocation.SymbolTableIndex = i;
			relocation.Type = ImageRelAmd64Addr64;
		}

		// Memory for symbols
		header.PointerToSymbolTable = u32(memoryBlock.writtenSize);
		header.NumberOfSymbols = loopbackCount * 2;
		auto symbols = (ImageSymbol*)allocate(header.NumberOfSymbols * sizeof(ImageSymbol));

		// Write string table
		u64 stringStart = memoryBlock.writtenSize;
		auto& stringTableSize = *(u32*)allocate(4);
		Vector<u32> symbolsToAdd;
		for (auto& str : neededLoopbacks)
		{
			symbolsToAdd.push_back(u32(memoryBlock.writtenSize - stringStart));
			write(str.data(), str.size()+1);
		}
		stringTableSize = u32(memoryBlock.writtenSize - stringStart);
		
		// Write symbols
		for (u32 i=0; i!=loopbackCount; ++i)
		{
			auto& symbol = symbols[i];
			symbol.N.Name.Long = symbolsToAdd[i] + 6; // Remove __imp_
			symbol.SectionNumber = ImageSymUndefined;
			symbol.StorageClass = ImageSymClassExternal;
		}
		for (u32 i=0; i!=loopbackCount; ++i)
		{
			auto& symbol = symbols[i + loopbackCount];
			symbol.N.Name.Long = symbolsToAdd[i];
			symbol.SectionNumber = textSessionIndex + 1;
			symbol.StorageClass = ImageSymClassExternal;
			symbol.Value = i * 8;
		}

		return true;
	}

	template<typename SymbolType> void ObjectFileCoff::CalculateImports(Logger& logger, Vector<u32>& outImports)
	{
		// Calculate how much more memory we need by counting imports
		auto symbols = (SymbolType*)(m_data + m_info.symbolsMemPos);
		std::string tmp;

		for (u32 i=0; i!=m_info.symbolCount; ++i)
		{
			auto& symbol = symbols[i];
			if (symbol.StorageClass != ImageSymClassExternal)
				continue;
			if (symbol.SectionNumber != ImageSymUndefined)
				continue;
			AnsiStringView symbolName = GetSymbolName(symbol, m_data, m_info.stringTableMemPos);
			if (!symbolName.StartsWith("__imp_", 6))
				continue;
			symbolName.strBegin += 6;
			auto findIt = m_loopbacksToAdd.find(symbolName.ToString(tmp));
			if (findIt == m_loopbacksToAdd.end())
				continue;
			m_loopbacksToAdd.erase(findIt);
			outImports.push_back(symbol.N.Name.Long);
		}
	}

	template<typename SymbolType>
	void ObjectFileCoff::WriteImports(Logger& logger, u8* newData, Info& newInfo, const Vector<u32>& symbolsToAdd)
	{
		u32 importsToFixCount = u32(symbolsToAdd.size());

		// Copy header and sections
		u32 offsetToAfterLastSection = m_info.sectionsMemOffset + m_info.sectionCount*sizeof(ImageSectionHeader);
		memcpy(newData, m_data, offsetToAfterLastSection);

		// Add another section
		u32 newSectionIndex = m_info.sectionCount;
		auto& newSection = *(ImageSectionHeader*)(newData + offsetToAfterLastSection);
		memset(&newSection, 0, sizeof(ImageSectionHeader));
		memcpy(newSection.Name, ".text$mn", 8);

		// Add dummy memory for relocations
		u32 newRelocationVirtualMemPos = offsetToAfterLastSection + sizeof(ImageSectionHeader);
		u32 newRelocationVirtualMemSize = 8 * importsToFixCount;
		memset(newData + newRelocationVirtualMemPos, 0, newRelocationVirtualMemSize);

		u32 newRelocationsPos = newRelocationVirtualMemPos + newRelocationVirtualMemSize;
		u32 newRelocationsSize = sizeof(ImageRelocation) * importsToFixCount;
		newSection.PointerToRelocations = newRelocationsPos;
		newSection.NumberOfRelocations = u16(importsToFixCount);
		newSection.PointerToRawData = newRelocationVirtualMemPos;
		newSection.SizeOfRawData = newRelocationVirtualMemSize;
		newSection.Characteristics = ImageScnCntCode | ImageScnMemExecute | ImageScnMemRead;
		UBA_ASSERT(importsToFixCount <= 65535);

		// Add the new relocations and raw memory
		u32 newSymbolIndex = m_info.symbolCount;
		memset(newData + newRelocationsPos, 0, sizeof(ImageRelocation) *  importsToFixCount);
		for (u32 i=0; i!=importsToFixCount; ++i)
		{
			auto& relocation = ((ImageRelocation*)(newData + newRelocationsPos))[i];
			relocation.VirtualAddress = 8 * i;
			relocation.SymbolTableIndex = newSymbolIndex + i;
			relocation.Type = ImageRelAmd64Addr64;
		}

		// Offset of everything after new section and relocations
		u32 memoryOffset = newRelocationsPos + newRelocationsSize - offsetToAfterLastSection;

		u32 symbolTablePos;
		u32 symbolTableSize;
		if (m_isBigObj)
		{
			auto& header = *(AnonObjectHeaderBigobj*)newData;
			++header.NumberOfSections;
			header.PointerToSymbolTable += memoryOffset;
			symbolTablePos = header.PointerToSymbolTable;
			symbolTableSize = header.NumberOfSymbols * sizeof(SymbolType);
			header.NumberOfSymbols += importsToFixCount*2;
		}
		else
		{
			auto& header = *(ImageFileHeader*)newData;
			++header.NumberOfSections;
			header.PointerToSymbolTable += memoryOffset;
			symbolTablePos = header.PointerToSymbolTable;
			symbolTableSize = header.NumberOfSymbols * sizeof(SymbolType);
			header.NumberOfSymbols += importsToFixCount*2;
		}

		u32 offsetToAfterSymbolTable = symbolTablePos + symbolTableSize;

		// Copy everything after sections til end of symbol table
		u64 nextToCopySize = m_info.symbolsMemPos + symbolTableSize - offsetToAfterLastSection;
		memcpy(newData + offsetToAfterLastSection + memoryOffset, m_data + offsetToAfterLastSection, nextToCopySize);

		newInfo.symbolsMemPos += memoryOffset;

		u32 newSymbolsPos = offsetToAfterSymbolTable;
		u32 newSymbolsSize = sizeof(SymbolType)*importsToFixCount*2;
		auto newSymbols = (SymbolType*)(newData + newSymbolsPos);
		memset(newSymbols, 0, newSymbolsSize);
		for (u32 i=0; i!=importsToFixCount; ++i)
		{
			auto& symbol = newSymbols[i];
			symbol.N.Name.Long = symbolsToAdd[i] + 6; // Remove __imp_
			symbol.SectionNumber = ImageSymUndefined;
			symbol.StorageClass = ImageSymClassExternal;
		}
		for (u32 i=0; i!=importsToFixCount; ++i)
		{
			auto& symbol = newSymbols[i + importsToFixCount];
			symbol.N.Name.Long = symbolsToAdd[i];
			symbol.SectionNumber = (decltype(SymbolType::SectionNumber))(newSectionIndex + 1);
			symbol.StorageClass = ImageSymClassExternal;
			symbol.Value = i * 8;
		}

		u64 lastToCopySize = m_dataSize - m_info.stringTableMemPos;
		memcpy(newData + newSymbolsPos + newSymbolsSize, m_data + m_info.stringTableMemPos, lastToCopySize);


		// Traverse all sections and update their PointerToRawData
		auto sections = (ImageSectionHeader*)(newData + m_info.sectionsMemOffset);
		for (u32 i=0; i!=m_info.sectionCount; ++i)
		{
			auto& section = sections[i];
			if (section.PointerToRawData)
			{
				UBA_ASSERT(section.PointerToRawData < symbolTablePos);
				section.PointerToRawData += memoryOffset;
			}
			if (section.PointerToRelocations)
			{
				UBA_ASSERT(section.PointerToRelocations < symbolTablePos);
				section.PointerToRelocations += memoryOffset;
			}
			if (section.PointerToLinenumbers)
			{
				UBA_ASSERT(section.PointerToLinenumbers < symbolTablePos);
				section.PointerToLinenumbers += memoryOffset;
			}
		}
	}

	template<typename SymbolType> void ObjectFileCoff::RemoveSymbols(Logger& logger, u8* newData, Info& newInfo)
	{
		auto symbols = (SymbolType*)(newData + newInfo.symbolsMemPos);
		auto sections = (ImageSectionHeader*)(newData + newInfo.sectionsMemOffset);

		std::string tmp;

		for (u32 i=0; i!=m_info.symbolCount; ++i)
		{
			auto& symbol = symbols[i];

			if (symbol.StorageClass != ImageSymClassExternal)
				continue;

			if (!symbol.SectionNumber)
				continue;

			AnsiStringView symbolName = GetSymbolName(symbol, m_data, m_info.stringTableMemPos);
			auto findIt = m_toRemove.find(symbolName.ToString(tmp));
			if (findIt == m_toRemove.end())
				continue;

			auto& section = sections[symbol.SectionNumber-1];

			symbol.SectionNumber = ImageSymUndefined;

			if (section.Characteristics & ImageScnLnkComdat)
				memset(&section, 0, sizeof(ImageSectionHeader));
		}
	}
}
