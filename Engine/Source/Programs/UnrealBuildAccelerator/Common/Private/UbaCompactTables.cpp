// Copyright Epic Games, Inc. All Rights Reserved.

#include "UbaCompactTables.h"
#include "UbaBinaryReaderWriter.h"

namespace uba
{
	CompactPathTable::CompactPathTable(u64 reserveSize, Version version, bool caseInsensitive, u64 reservePathCount, u64 reserveSegmentCount)
	:	m_reserveSize(reserveSize)
	,	m_version(version)
	,	m_caseInsensitive(caseInsensitive)
	{
		if (reservePathCount)
			m_offsets.reserve(reservePathCount);
		if (reserveSegmentCount)
			m_segmentOffsets.reserve(reserveSegmentCount);
	}

	u32 CompactPathTable::Add(const tchar* str, u64 strLen, u32* outRequiredCasTableSize)
	{
		SCOPED_WRITE_LOCK(m_lock, lock);
		u32 res = AddNoLock(str, strLen);
		if (outRequiredCasTableSize)
			*outRequiredCasTableSize = u32(m_mem.writtenSize);
		return res;
	}

	u32 CompactPathTable::AddNoLock(const tchar* str, u64 strLen)
	{
		if (!m_mem.memory)
			m_mem.Init(m_reserveSize);
		if (!m_mem.writtenSize)
			m_mem.AllocateNoLock(1, 1, TC(""));

		const tchar* stringKeyString = str;
		StringBuffer<MaxPath> tempStringKeyStr;
		if (m_caseInsensitive)
			stringKeyString = tempStringKeyStr.Append(str).MakeLower().data;

		return InternalAdd(str, stringKeyString, strLen);
	}

	u32 CompactPathTable::InternalAdd(const tchar* str, const tchar* stringKeyString, u64 strLen)
	{
		StringKey key = ToStringKeyNoCheck(stringKeyString, strLen);
		auto insres = m_offsets.try_emplace(key);
		if (!insres.second)
			return insres.first->second;
			
		const tchar* seg = str;
		u32 parentOffset = 0;
		
		for (const tchar* it = str + strLen - 1; it > str; --it)
		{
			if (*it != PathSeparator)
				continue;

			parentOffset = InternalAdd(str, stringKeyString, it - str);
			seg = it + 1;
			break;
		}

		u64 segLen = strLen - (seg - str);
		u8 bytesForParent = Get7BitEncodedCount(parentOffset);

		if (m_version == V0)
		{
			u64 bytesForString = GetStringWriteSize(seg, segLen);

			u64 memSize = bytesForParent + bytesForString;
			u8* mem = (u8*)m_mem.AllocateNoLock(memSize, 1, TC(""));
			BinaryWriter writer(mem, 0, memSize);
			writer.Write7BitEncoded(parentOffset);
			writer.WriteString(seg, segLen);
			insres.first->second = u32(mem - m_mem.memory);
			return insres.first->second;
		}
		else
		{
			StringKey segmentKey = ToStringKeyNoCheck(seg, segLen);
			auto insres2 = m_segmentOffsets.try_emplace(segmentKey);
			if (insres2.second)
			{
				// Put string directly after current element and set segment offset to 0
				u64 bytesForString = GetStringWriteSize(seg, segLen);
				u64 memSize = bytesForParent + 1 + bytesForString;
				u8* mem = (u8*)m_mem.AllocateNoLock(memSize, 1, TC(""));
				BinaryWriter writer(mem, 0, memSize);
				writer.Write7BitEncoded(parentOffset);
				writer.Write7BitEncoded(0);
				writer.WriteString(seg, segLen);
				u32 offset = u32(mem - m_mem.memory);
				insres.first->second = offset;
				insres2.first->second = offset + bytesForParent + 1;
				return offset;
			}

			#if 0
			StringBuffer<> temp;
			BinaryReader reader(m_mem.memory, insres2.first->second, 1000);
			reader.ReadString(temp);
			UBA_ASSERT(temp.count == segLen && wcsncmp(temp.data, seg, segLen) == 0);
			#endif

			u32 strOffset = insres2.first->second;
			u64 memSize = bytesForParent + Get7BitEncodedCount(strOffset);
			u8* mem = (u8*)m_mem.AllocateNoLock(memSize, 1, TC(""));
			BinaryWriter writer(mem, 0, memSize);
			writer.Write7BitEncoded(parentOffset);
			writer.Write7BitEncoded(strOffset);
			insres.first->second = u32(mem - m_mem.memory);
			return insres.first->second;
		}
	}

	void CompactPathTable::GetString(StringBufferBase& out, u64 offset) const
	{
		#if UBA_DEBUG
		{
			SCOPED_READ_LOCK(const_cast<CompactPathTable*>(this)->m_lock, lock)
			UBA_ASSERTF(offset < m_mem.writtenSize, TC("Reading path key from offset %llu which is out of bounds (Max %llu)"), offset, m_mem.writtenSize);
		}
		#endif

		u32 offsets[256];
		offsets[0] = u32(offset);
		u32 offsetCount = 0;

		BinaryReader reader(m_mem.memory, offset, m_mem.writtenSize);

		while (offset)
		{
			++offsetCount;
			UBA_ASSERT(offsetCount < sizeof_array(offsets));
			reader.SetPosition(offset);
			offset = (u32)reader.Read7BitEncoded();
			offsets[offsetCount] = u32(offset);
		}

		if (m_version == V0)
		{
			bool isFirst = true;
			for (u32 i=offsetCount;i; --i)
			{
				reader.SetPosition(offsets[i-1]);
				reader.Read7BitEncoded();

				if (!isFirst)
					out.Append(PathSeparator);
				isFirst = false;
				reader.ReadString(out);
			}
		}
		else
		{
			bool isFirst = true;
			for (u32 i=offsetCount;i; --i)
			{
				reader.SetPosition(offsets[i-1]);
				reader.Read7BitEncoded();
				u32 strOffset = u32(reader.Read7BitEncoded());
				if (strOffset != 0)
					reader.SetPosition(strOffset);

				if (!isFirst)
					out.Append(PathSeparator);
				isFirst = false;
				reader.ReadString(out);
			}
		}
	}

	u8* CompactPathTable::GetMemory()
	{
		return m_mem.memory;
	}

	u32 CompactPathTable::GetSize()
	{
		SCOPED_READ_LOCK(m_lock, lock2)
		return u32(m_mem.writtenSize);
	}

	void CompactPathTable::ReadMem(BinaryReader& reader, bool populateLookup)
	{
		if (!m_mem.memory)
			m_mem.Init(m_reserveSize);

		u64 writtenSize = m_mem.writtenSize;
		u64 left = reader.GetLeft();
		void* mem = m_mem.AllocateNoLock(left, 1, TC(""));
		reader.ReadBytes(mem, left);

		if (!populateLookup)
			return;

		BinaryReader reader2(m_mem.memory, writtenSize, m_mem.writtenSize);
		if (!writtenSize)
			reader2.Skip(1);

		if (m_version == V0)
		{
			while (reader2.GetLeft())
			{
				u32 offset = u32(reader2.GetPosition());
				reader2.Read7BitEncoded();
				reader2.SkipString();
				StringBuffer<> str;
				GetString(str, offset);
				if (m_caseInsensitive)
					str.MakeLower();
				m_offsets.try_emplace(ToStringKeyNoCheck(str.data, str.count), offset);
			}
		}
		else
		{
			while (reader2.GetLeft())
			{
				u32 offset = u32(reader2.GetPosition());
				reader2.Read7BitEncoded();
				u64 stringOffset = reader2.Read7BitEncoded();
				if (!stringOffset)
				{
					u32 strOffset = u32(reader2.GetPosition());
					StringBuffer<> seg;
					reader2.ReadString(seg);
					m_segmentOffsets.try_emplace(ToStringKeyNoCheck(seg.data, seg.count), strOffset);
				}
				StringBuffer<> str;
				GetString(str, offset);
				if (m_caseInsensitive)
					str.MakeLower();
				m_offsets.try_emplace(ToStringKeyNoCheck(str.data, str.count), offset);
			}
		}
	}

	void CompactPathTable::Swap(CompactPathTable& other)
	{
		m_offsets.swap(other.m_offsets);
		m_segmentOffsets.swap(other.m_segmentOffsets);
		m_mem.Swap(other.m_mem);
		u64 rs = m_reserveSize;
		m_reserveSize = other.m_reserveSize;
		other.m_reserveSize = rs;
		Version v = m_version;
		m_version = other.m_version;
		other.m_version = v;
	}

	CompactCasKeyTable::CompactCasKeyTable(u64 reserveSize, u64 reserveOffsetsCount)
	{
		m_reserveSize = reserveSize;
		if (reserveOffsetsCount)
			m_offsets.reserve(reserveOffsetsCount);
	}

	CompactCasKeyTable::~CompactCasKeyTable()
	{
		for (auto& kv : m_offsets)
			if (kv.second.count > 1)
				delete[] kv.second.stringAndCasKeyOffsets;

	}

	u32 CompactCasKeyTable::Add(const CasKey& casKey, u64 stringOffset, u32* outRequiredCasTableSize)
	{
		SCOPED_WRITE_LOCK(m_lock, lock2)
		if (!m_mem.memory)
			m_mem.Init(m_reserveSize);

		bool added = false;
		u32* casKeyOffset = InternalAdd(casKey, stringOffset, added);

		if (added)
		{
			u8 bytesForStringOffset = Get7BitEncodedCount(stringOffset);
			u8* mem = (u8*)m_mem.AllocateNoLock(bytesForStringOffset + sizeof(CasKey), 1, TC(""));
			BinaryWriter writer(mem, 0, 1000);
			writer.Write7BitEncoded(stringOffset);
			writer.WriteCasKey(casKey);
			*casKeyOffset = u32(mem - m_mem.memory);
			if (outRequiredCasTableSize)
				*outRequiredCasTableSize = (u32)m_mem.writtenSize;
		}
		else if (outRequiredCasTableSize)
		{
			BinaryReader reader(m_mem.memory, *casKeyOffset, ~0u);
			reader.Read7BitEncoded();
			*outRequiredCasTableSize = Max(*outRequiredCasTableSize, u32(reader.GetPosition() + sizeof(CasKey)));
		}
		return *casKeyOffset;
	}

	u32* CompactCasKeyTable::InternalAdd(const CasKey& casKey, u64 stringOffset, bool& outAdded)
	{
		auto insres = m_offsets.try_emplace(casKey);
		Value& value = insres.first->second;
		if (insres.second)
		{
			value.count = 1;
			value.single.stringOffset = u32(stringOffset);
			outAdded = true;
			return &value.single.casKeyOffset;
		}
		
		if (value.count == 1)
		{
			if (value.single.stringOffset == stringOffset)
				return &value.single.casKeyOffset;

			u32* newOffsets = new u32[4];
			newOffsets[0] = value.single.stringOffset;
			newOffsets[1] = value.single.casKeyOffset;
			newOffsets[2] = u32(stringOffset);
			value.stringAndCasKeyOffsets = newOffsets;
			value.count = 2;
			outAdded = true;
			return &newOffsets[3];
		}

		for (u32 i=0, e=value.count*2; i!=e; i+=2)
			if (value.stringAndCasKeyOffsets[i] == stringOffset)
				return &value.stringAndCasKeyOffsets[i + 1];

		u32* newOffsets = new u32[(value.count+1)*2];
		memcpy(newOffsets, value.stringAndCasKeyOffsets, value.count*2*sizeof(u32));
		newOffsets[value.count*2] = u32(stringOffset);
		delete[] value.stringAndCasKeyOffsets;
		value.stringAndCasKeyOffsets = newOffsets;
		++value.count;
		outAdded = true;
		return &newOffsets[value.count*2 - 1];
	}

	void CompactCasKeyTable::GetKey(CasKey& outKey, u64 offset) const
	{
		BinaryReader reader(m_mem.memory, offset, ~0u);
		reader.Read7BitEncoded();
		outKey = reader.ReadCasKey();
	}

	void CompactCasKeyTable::GetPathAndKey(StringBufferBase& outPath, CasKey& outKey, const CompactPathTable& pathTable, u64 offset) const
	{
		#if UBA_DEBUG
		{
			SCOPED_READ_LOCK(const_cast<CompactCasKeyTable*>(this)->m_lock, lock)
			UBA_ASSERTF(offset + sizeof(CasKey) < m_mem.writtenSize, TC("Reading cas key from offset %llu which is out of bounds (Max %llu)"), offset + sizeof(CasKey), m_mem.writtenSize);
		}
		#endif

		BinaryReader reader(m_mem.memory, offset, ~0u);
		u32 stringOffset = (u32)reader.Read7BitEncoded();
		outKey = reader.ReadCasKey();
		pathTable.GetString(outPath, stringOffset);
	}

	u8* CompactCasKeyTable::GetMemory()
	{
		return m_mem.memory;
	}

	u32 CompactCasKeyTable::GetSize()
	{
		SCOPED_READ_LOCK(m_lock, lock2)
		return u32(m_mem.writtenSize);
	}

	void CompactCasKeyTable::ReadMem(BinaryReader& reader, bool populateLookup)
	{
		if (!m_mem.memory)
			m_mem.Init(m_reserveSize);

		u64 writtenSize = m_mem.writtenSize;

		u64 left = reader.GetLeft();
		void* mem = m_mem.AllocateNoLock(left, 1, TC(""));
		reader.ReadBytes(mem, left);

		if (!populateLookup)
			return;

		BinaryReader reader2(m_mem.memory, writtenSize, m_mem.writtenSize);
		while (reader2.GetLeft())
		{
			u32 offset = u32(reader2.GetPosition());
			u64 stringOffset = reader2.Read7BitEncoded();
			CasKey casKey = reader2.ReadCasKey();
			bool added = false;
			u32* casKeyOffset = InternalAdd(casKey, stringOffset, added);
			UBA_ASSERT(added);
			*casKeyOffset = offset;
		}
	}

	void CompactCasKeyTable::Swap(CompactCasKeyTable& other)
	{
		m_offsets.swap(other.m_offsets);
		m_mem.Swap(other.m_mem);
		u64 rs = m_reserveSize;
		m_reserveSize = other.m_reserveSize;
		other.m_reserveSize = rs;
	}
}
