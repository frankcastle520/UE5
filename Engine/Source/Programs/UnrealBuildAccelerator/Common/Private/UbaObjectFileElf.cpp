// Copyright Epic Games, Inc. All Rights Reserved.

#include "UbaObjectFileElf.h"
#include "UbaFileAccessor.h"

namespace uba
{
	struct Elf64Header
	{
		unsigned char	e_ident[16];	// Magic number and other info
		u16	e_type;			// Object file type
		u16	e_machine;		// Architecture
		u32	e_version;		// Object file version
		u64	e_entry;		// Entry point virtual address
		u64	e_phoff;		// Program header table file offset
		u64	e_shoff;		// Section header table file offset
		u32	e_flags;		// Processor-specific flags
		u16	e_ehsize;		// ELF header size in bytes
		u16	e_phentsize;	// Program header table entry size
		u16	e_phnum;		// Program header table entry count
		u16	e_shentsize;	// Section header table entry size
		u16	e_shnum;		// Section header table entry count
		u16	e_shstrndx;		// Section header string table index
	};

	struct Elf64SectionHeader
	{
		u32	sh_name;		// Section name (string tbl index)
		u32	sh_type;		// Section type
		u64	sh_flags;		// Section flags
		u64	sh_addr;		// Section virtual addr at execution
		u64	sh_offset;		// Section file offset
		u64	sh_size;		// Section size in bytes
		u32	sh_link;		// Link to another section
		u32	sh_info;		// Additional section information
		u64	sh_addralign;	// Section alignment
		u64	sh_entsize;		// Entry size if section holds table
	};

	struct Elf64Sym
	{
		u32	st_name;		// Symbol name (string tbl index)
		u8	st_info;		// Symbol type and binding
		u8	st_other;		// Symbol visibility
		u16	st_shndx;		// Section index
		u64	st_value;		// Symbol value
		u64	st_size;		// Symbol size
	};

	struct Elf64Rel
	{
		u64	r_offset;
		u64	r_info;
	};
 
	struct Elf64Rela
	{
		u64	r_offset;
		u64	r_info;
		u64	r_addend;
	};

	#define ELF64_R_SYM(i)			((i) >> 32)
	#define ELF64_R_TYPE(i)			((i) & 0xffffffff)
	#define ELF64_R_INFO(sym,type)		((((u64) (sym)) << 32) + (type))

	#define ELF32_ST_VISIBILITY(o)	((o) & 0x03)
	#define ELF64_ST_VISIBILITY(o)	ELF32_ST_VISIBILITY (o)

	#define EM_X86_64	62		// AMD x86-64 architecture

	#define SHT_PROGBITS  1		// Program data
	#define SHT_SYMTAB	  2		// Symbol table
	#define SHT_STRTAB	  3
	#define SHT_RELA	  4		// Relocation entries with addends
	#define SHT_DYNAMIC	  6		// Dynamic linking information
	#define SHT_REL		  9		// Relocation entries, no addends
	#define SHT_DYNSYM	  11	// Dynamic linker symbol table
	#define SHT_SYMTAB_SHNDX  18 // Extended section indeces

	#define STT_NOTYPE	0		// Symbol type is unspecified
	#define STT_OBJECT	1		// Symbol is a data object
	#define STT_FUNC	2		// Symbol is a code object
	#define STT_SECTION	3		// Symbol associated with a section

	#define STB_LOCAL	0		// Local symbol
	#define STB_GLOBAL	1		// Global symbol
	#define STB_WEAK	2		// Weak symbol

	#define STV_DEFAULT	0		// Default symbol visibility rules
	#define STV_INTERNAL	1	// Processor specific hidden class
	#define STV_HIDDEN	2		// Sym unavailable in other modules
	#define STV_PROTECTED	3	// Not preemptible, not exported

	#define ELF32_ST_BIND(val)		(((unsigned char) (val)) >> 4)
	#define ELF32_ST_TYPE(val)		((val) & 0xf)
	#define ELF32_ST_INFO(bind, type)	(((bind) << 4) + ((type) & 0xf))

	// Both Elf32_Sym and Elf64_Sym use the same one-byte st_info field.
	#define ELF64_ST_BIND(val)		ELF32_ST_BIND(val)
	#define ELF64_ST_TYPE(val)		ELF32_ST_TYPE (val)
	#define ELF64_ST_INFO(bind, type)	ELF32_ST_INFO ((bind), (type))

	bool IsElfFile(const u8* data, u64 dataSize)
	{
		constexpr u8 magic[] = { 0x7f, 'E', 'L', 'F' };
		return dataSize > 4 && memcmp(data, magic, sizeof(magic)) == 0;
	}


	ObjectFileElf::ObjectFileElf()
	{
		m_type = ObjectFileType_Elf;
	}


	const char* GetSymbolName(const Elf64SectionHeader* sections, const Elf64Sym& symbol, const char* symTableNames, const char* dynTableNames, const char* sectionNamesTable)
	{
		u8 symbolType = ELF64_ST_TYPE(symbol.st_info);
		if (symbolType == STT_FUNC || symbolType == STT_NOTYPE || symbolType == STT_OBJECT)
			return symTableNames + symbol.st_name;
		if (symbolType == SHT_DYNSYM)
			return dynTableNames + symbol.st_name;
		if (symbolType == STT_SECTION && symbol.st_shndx != 65535)
			return sectionNamesTable + sections[symbol.st_shndx].sh_name;
		return "";
	}

	bool ObjectFileElf::Parse(Logger& logger, const tchar* hint)
	{
		auto& header = *(Elf64Header*)m_data;
		if (header.e_ident[4] != 2) // 64-bit  (1 is 32-bit)
			return false;
		if (header.e_ident[5] != 1) // Little endian
			return false;
		if (header.e_ident[6] != 1) // Version
			return false;
		//if (header.e_type != 1 && header.e_type != 0xfe0c) // Relocatable file
		//	return false;

		UBA_ASSERT(sizeof(Elf64SectionHeader) == header.e_shentsize);

		auto sections = (Elf64SectionHeader*)(m_data + header.e_shoff);

		auto& namesSection = sections[header.e_shstrndx];
		UBA_ASSERT(namesSection.sh_type == SHT_STRTAB);
		char* sectionNamesTable = (char*)m_data + namesSection.sh_offset;

		u64 sectionCount = header.e_shnum ? header.e_shnum : sections[0].sh_size;

		for (u64 i=0; i!=sectionCount; ++i)
		{
			auto& section = sections[i];
			char* sectionName = sectionNamesTable + section.sh_name;

			if (section.sh_type == SHT_STRTAB)
			{
				if (strcmp(sectionName, ".strtab") == 0)
					m_symTableNamesOffset = section.sh_offset;
				else if (strcmp(sectionName, ".dynstr") == 0)
					m_dynTableNamesOffset = section.sh_offset;
			}
			else if (section.sh_type == SHT_PROGBITS)
			{
				if (strcmp(sectionName, ".linker_cmd") == 0)
					m_useVisibilityForExports = false;
			}
		}

		char* symTableNames = (char*)m_data + m_symTableNamesOffset;
		char* dynTableNames = (char*)m_data + m_dynTableNamesOffset;

		for (u64 i=0; i!=sectionCount; ++i)
		{
			auto& section = sections[i]; 

			char* sectionName = sectionNamesTable + section.sh_name;
			//printf("TYPE: %3llu - %14u %-30s  Offset: 0x%llx Size: %llu\n", i, section.sh_type, sectionName, section.sh_offset, section.sh_size);

			if (section.sh_type == SHT_SYMTAB || section.sh_type == SHT_DYNSYM)
			{
				UBA_ASSERT(section.sh_entsize == sizeof(Elf64Sym));
				auto symbols = (Elf64Sym*)(m_data + section.sh_offset);
				u64 symbolCount = section.sh_size / sizeof(Elf64Sym);
				for (u64 j=0; j!=symbolCount; ++j)
				{
					auto& symbol = symbols[j];
					const char* symbolName = GetSymbolName(sections, symbol, symTableNames, dynTableNames, sectionNamesTable);
					if (!*symbolName)
						continue;

					u8 symbolType = ELF64_ST_TYPE(symbol.st_info);
					u32 symbolBinding = ELF64_ST_BIND(symbol.st_info);
					u32 symbolVisibility = ELF64_ST_VISIBILITY(symbol.st_other);

					//if (strstr(symbolName, "_ZN11TSubclassOfI13UAnimInstance"))
					//	printf("");

					if (symbolBinding == STB_GLOBAL)
					{
						if (symbolType != STT_NOTYPE)
						{
							// This is an ugly hack because some platforms are not exporting vtable even though they should
							if (symbolVisibility == STV_HIDDEN && strncmp(symbolName, "_ZTV", 4) == 0)
								m_exports.emplace(symbolName, ExportInfo{std::string(), 0});//index++});

							if (m_useVisibilityForExports && symbolVisibility != STV_HIDDEN)
								m_exports.emplace(symbolName, ExportInfo{std::string(), 0});//index++});
						}
						else
							m_imports.emplace(symbolName);//index++});
					}
					else if (symbolBinding == STB_WEAK)
					{
						if (symbolType != STT_NOTYPE)
						{
							if (symbolVisibility == STV_DEFAULT || symbolVisibility == STV_PROTECTED)
							{
								// Another ugly hack because of dll exported template specialization
							}
							else
							{
								if (strcmp(symbolName, "_ZNK5Chaos24TBoundingVolumeHierarchyI6TArrayIPNS_7TSphereIdLi3EEE22TSizedDefaultAllocatorILi32EEES1_IiS6_EdLi3EE26FindAllIntersectionsHelperERKNS_8TBVHNodeIdLi3EEERKNS_7TVectorIdLi3EEE") == 0)
									m_exports.emplace(symbolName, ExportInfo{std::string(), 0});//index++});
								if (strcmp(symbolName, "_ZN5Chaos24TBoundingVolumeHierarchyI6TArrayIPNS_7TSphereIdLi3EEE22TSizedDefaultAllocatorILi32EEES1_IiS6_EdLi3EEC1ERKS7_ibd") == 0)
									m_exports.emplace(symbolName, ExportInfo{std::string(), 0});//index++});
							}
						}
					}
				}
			}
			else if (!m_useVisibilityForExports && section.sh_type == SHT_PROGBITS)
			{
				if (strcmp(sectionName, ".linker_cmd") == 0)
				{
					u8* it = m_data + section.sh_offset;
					u8* end = it + section.sh_size;
					while (it < end)
					{
						it += 4;
						char* symbolName = (char*)it;
						m_exports.emplace(symbolName, ExportInfo{std::string(), 0});
						it += strlen(symbolName) + 1;
					}
				}
			}
		}

		return true;
	}

	bool ObjectFileElf::StripExports(Logger& logger, u8* newData, const UnorderedSymbols& allExternalImports)
	{
		auto& header = *(Elf64Header*)newData;
		auto sections = (Elf64SectionHeader*)(newData + header.e_shoff);
		char* sectionNamesTable = (char*)newData + sections[header.e_shstrndx].sh_offset;
		u64 sectionCount = header.e_shnum ? header.e_shnum : sections[0].sh_size;

		if (m_useVisibilityForExports)
		{
			char* symTableNames = (char*)newData + m_symTableNamesOffset;
			char* dynTableNames = (char*)newData + m_dynTableNamesOffset;

			for (u64 i=0; i!=sectionCount; ++i)
			{
				auto& section = sections[i];

				if (section.sh_type != SHT_SYMTAB && section.sh_type != SHT_DYNSYM)
					continue;

				auto symbols = (Elf64Sym*)(newData + section.sh_offset);
				u64 symbolCount = section.sh_size / sizeof(Elf64Sym);
				for (u64 j=0; j!=symbolCount; ++j)
				{
					auto& symbol = symbols[j];
					const char* symbolName = GetSymbolName(sections, symbol, symTableNames, dynTableNames, sectionNamesTable);
					if (!*symbolName)
						continue;

					u8 symbolType = ELF64_ST_TYPE(symbol.st_info);
					u32 symbolBinding = ELF64_ST_BIND(symbol.st_info);
					u32 symbolVisibility = ELF64_ST_VISIBILITY(symbol.st_other);

					if (symbolBinding == STB_GLOBAL)
					{
						if (symbolType != STT_NOTYPE)
						{
							if (symbolVisibility == STV_HIDDEN && strncmp(symbolName, "_ZTV", 4) == 0)
								symbol.st_other = STV_PROTECTED;
							//else if (symbolVisibility != STV_HIDDEN)
							//	symbol.st_other = STV_HIDDEN;
						}
					}
					else if (symbolBinding == STB_WEAK && symbolType != STT_NOTYPE)
					{
						if (symbolVisibility == STV_DEFAULT)
						{
						}
						else if (symbolVisibility == STV_HIDDEN)
						{
							if (strcmp(symbolName, "_ZNK5Chaos24TBoundingVolumeHierarchyI6TArrayIPNS_7TSphereIdLi3EEE22TSizedDefaultAllocatorILi32EEES1_IiS6_EdLi3EE26FindAllIntersectionsHelperERKNS_8TBVHNodeIdLi3EEERKNS_7TVectorIdLi3EEE") == 0)
								symbol.st_other = STV_PROTECTED;
							if (strcmp(symbolName, "_ZN5Chaos24TBoundingVolumeHierarchyI6TArrayIPNS_7TSphereIdLi3EEE22TSizedDefaultAllocatorILi32EEES1_IiS6_EdLi3EEC1ERKS7_ibd") == 0)
								symbol.st_other = STV_PROTECTED;
						}

					}
				}
			}
		}
		else
		{
			for (u64 i=0; i!=sectionCount; ++i)
			{
				auto& section = sections[i];
				char* sectionName = sectionNamesTable + section.sh_name;

				// Remove all but first symbol.. 
				// TODO Ideally we want to remove all symbols but then it seems like no stub file is created, maybe we can put a dummy symbol in there?
				if (section.sh_type != SHT_PROGBITS || strcmp(sectionName, ".linker_cmd") != 0)
					continue;

				u64 oldSize = section.sh_size;
				if (!oldSize)
					return true;
				u8* begin = newData + section.sh_offset;
				u8* it = begin;

				it += 4;
				it += strlen((char*)it) + 1;

				section.sh_size = it - begin;
				memset(it, 0, oldSize - section.sh_size);

				break;
			}
		}
		return true;
	}

	bool ObjectFileElf::CreateExtraFile(Logger& logger, const StringView& platform, MemoryBlock& memoryBlock, const UnorderedSymbols& allExternalImports, const UnorderedSymbols& allInternalImports, const UnorderedExports& allExports, bool includeExportsInFile)
	{
		u8* data = (u8*)memoryBlock.Allocate(sizeof(Elf64Header) + sizeof(Elf64SectionHeader) + 1, 1, TC(""));
		auto& header = *(Elf64Header*)data;

		header.e_ident[0] = 0x7f;
		header.e_ident[1] = 'E';
		header.e_ident[2] = 'L';
		header.e_ident[3] = 'F';
		header.e_ident[4] = 2;
		header.e_ident[5] = 1;
		header.e_ident[6] = 1;
		header.e_ident[7] = platform.Equals(TC("PS4")) ? 9 : 0; // OS ABI identification
		header.e_type = 1;
		header.e_machine = EM_X86_64;
		header.e_version = 1;
		header.e_ehsize = sizeof(Elf64Header);
		header.e_flags = 0;//0x04000000;
		header.e_shoff = sizeof(Elf64Header);
		header.e_shentsize = sizeof(Elf64SectionHeader);
		header.e_shnum = 1;
		header.e_shstrndx = 0;

		auto& section = *(Elf64SectionHeader*)(data + sizeof(Elf64Header));
		section.sh_name = 0;
		section.sh_type = 3;
		section.sh_flags = 0;//0xC35d00;
		section.sh_addralign = 1;
		section.sh_offset = sizeof(Elf64Header) + sizeof(Elf64SectionHeader);
		section.sh_size = 1;
		return true;
	}
}
