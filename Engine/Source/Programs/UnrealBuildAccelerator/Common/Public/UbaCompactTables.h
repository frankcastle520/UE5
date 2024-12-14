// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UbaHash.h"
#include "UbaMemory.h"

namespace uba
{
	struct BinaryReader;
	struct BinaryWriter;
	struct StringKey;

	class CompactPathTable
	{
	public:
		enum Version : u8 { V0, V1 };

		CompactPathTable(u64 reserveSize, Version version, bool caseSensitive, u64 reservePathCount = 0, u64 reserveSegmentCount = 0);

		u32 Add(const tchar* str, u64 strLen, u32* outRequiredCasTableSize = nullptr);
		u32 AddNoLock(const tchar* str, u64 strLen);

		void GetString(StringBufferBase& out, u64 offset) const;

		u8* GetMemory();
		u32 GetSize();

		void ReadMem(BinaryReader& reader, bool populateLookup);
		void Swap(CompactPathTable& other);

		u64 GetPathCount() { return m_offsets.size(); }
		u64 GetSegmentCount() { return m_segmentOffsets.size(); }

	private:
		u32 InternalAdd(const tchar* str, const tchar* stringKeyString, u64 strLen);
		ReaderWriterLock m_lock;
		MemoryBlock m_mem;
		UnorderedMap<StringKey, u32> m_offsets;
		UnorderedMap<StringKey, u32> m_segmentOffsets;
		u64 m_reserveSize;
		Version m_version;
		bool m_caseInsensitive;
	};

	class CompactCasKeyTable
	{
	public:
		CompactCasKeyTable(u64 reserveSize, u64 reserveOffsetsCount = 0);
		~CompactCasKeyTable();

		u32 Add(const CasKey& casKey, u64 stringOffset, u32* outRequiredCasTableSize = nullptr);

		template<typename Func>
		void TraverseOffsets(const CasKey& casKey, const Func& func) const;

		void GetKey(CasKey& outKey, u64 offset) const;
		void GetPathAndKey(StringBufferBase& outPath, CasKey& outKey, const CompactPathTable& pathTable, u64 offset) const;

		u8* GetMemory();
		u32 GetSize();
		ReaderWriterLock& GetLock() { return m_lock; }

		void ReadMem(BinaryReader& reader, bool populateLookup);
		void Swap(CompactCasKeyTable& other);

		u64 GetKeyCount() { return m_offsets.size(); }

	private:
		u32* InternalAdd(const CasKey& casKey, u64 stringOffset, bool& outAdded);

		ReaderWriterLock m_lock;
		MemoryBlock m_mem;
		struct Value
		{
			union
			{
				u32* stringAndCasKeyOffsets;
				struct
				{
					u32 stringOffset;
					u32 casKeyOffset;
				} single;
			};
			u32 count; // If count is 1, single is set, otherwise stringAndCasKeyOffsets is allocated and contains two offsets per entry
		};

		UnorderedMap<CasKey, Value> m_offsets;
		u64 m_reserveSize;
	};


	template<typename Func>
	void CompactCasKeyTable::TraverseOffsets(const CasKey& casKey, const Func& func) const
	{
		auto findIt = m_offsets.find(casKey);
		if (findIt == m_offsets.end())
			return;
		const Value& value = findIt->second;
		if (value.count == 1)
			func(value.single.casKeyOffset);
		else
			for (u32 i=1, e=value.count*2+1; i!=e; i+=2)
				func(value.stringAndCasKeyOffsets[i]);
	}
}
