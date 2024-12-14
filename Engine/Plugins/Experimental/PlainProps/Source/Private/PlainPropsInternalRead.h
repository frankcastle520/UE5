// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PlainPropsTypes.h"
#include "PlainPropsRead.h"
#include "PlainPropsInternalFormat.h"
#include <type_traits>

namespace PlainProps
{

template<typename MemberType, typename IndexType>
TConstArrayView<MemberType> GrabInnerRangeTypes(TConstArrayView<MemberType> InnerRangeTypes, IndexType& InOutIdx)
{
	const int32 StartIdx = InOutIdx;
	int32 Idx = StartIdx;
	while (InnerRangeTypes[Idx++].IsRange());
	
	InOutIdx = static_cast<IndexType>(Idx);
	return MakeArrayView(&InnerRangeTypes[StartIdx], Idx - StartIdx);
}

inline uint64 GrabRangeNum(ERangeSizeType MaxSize, FByteReader& ByteIt, FBitCacheReader& BitIt)
{
	switch(MaxSize)
	{
	case ERangeSizeType::Uni:	return BitIt.GrabNext(/* in-out */ ByteIt) ? 1 : 0;
	case ERangeSizeType::S8:	return IntCastChecked<uint64>(	ByteIt.Grab<int8>());
	case ERangeSizeType::U8:	return							ByteIt.Grab<uint8>();
	case ERangeSizeType::S16:	return IntCastChecked<uint64>(	ByteIt.Grab<int16>());
	case ERangeSizeType::U16:	return							ByteIt.Grab<uint16>();
	case ERangeSizeType::S32:	return IntCastChecked<uint64>(	ByteIt.Grab<int32>());
	case ERangeSizeType::U32:	return							ByteIt.Grab<uint32>();
	case ERangeSizeType::S64:	return IntCastChecked<uint64>(	ByteIt.Grab<int64>());
	case ERangeSizeType::U64:	return							ByteIt.Grab<uint64>();
	}
	check(false);
	return 0;
}

//////////////////////////////////////////////////////////////////////////

template<class T>
struct TSchemaIterator
{
	uintptr_t						Base;
	const uint32*					OffsetIt;
	
	void							operator++()							{ ++OffsetIt; }
	bool							operator!=(TSchemaIterator O) const		{ return OffsetIt != O.OffsetIt; }
	T&								operator*() const						{ return *reinterpret_cast<T*>(Base + *OffsetIt); }
};

template<class T>
struct TSchemaRange
{
	static constexpr bool bStructSchema = std::is_convertible_v<T, FStructSchema>;

	TSchemaRange(std::conditional_t<std::is_const_v<T>, const FSchemaBatch&, FSchemaBatch&> Batch)
	: Base(reinterpret_cast<uintptr_t>(&Batch))
	, Offsets(Batch.SchemaOffsets + (bStructSchema ? 0 : Batch.NumStructSchemas), bStructSchema ? Batch.NumStructSchemas : Batch.NumSchemas - Batch.NumStructSchemas)
	{
		static_assert(bStructSchema ^ std::is_convertible_v<T, FEnumSchema>);
	}
	
	TSchemaIterator<T>				begin() const		{ return { Base, Offsets.begin() }; }
	TSchemaIterator<T>				end() const			{ return { Base, Offsets.end() }; }
	T&								First() const		{ return *reinterpret_cast<T*>(Base + Offsets[0]); }
	T&								Last() const		{ return *reinterpret_cast<T*>(Base + Offsets.Last()); }

	uintptr_t						Base;
	TConstArrayView<uint32>			Offsets;
};

inline TSchemaRange<FEnumSchema>			GetEnumSchemas(FSchemaBatch& Batch)				{ return { Batch }; }
inline TSchemaRange<const FEnumSchema>		GetEnumSchemas(const FSchemaBatch& Batch)		{ return { Batch }; }
inline TSchemaRange<FStructSchema>			GetStructSchemas(FSchemaBatch& Batch)			{ return { Batch }; }
inline TSchemaRange<const FStructSchema>	GetStructSchemas(const FSchemaBatch& Batch)		{ return { Batch }; }

//////////////////////////////////////////////////////////////////////////

inline FMemoryView GetSchemaData(const FSchemaBatch& Batch)
{
	check(Batch.NumSchemas > 0);
	const uint8* BatchPtr = reinterpret_cast<const uint8*>(&Batch);
	TConstArrayView<uint32> Offsets(Batch.SchemaOffsets, Batch.NumSchemas);
	
	bool bHasEnums = Batch.NumSchemas > Batch.NumStructSchemas;
	uint32 LastSchemaSize = bHasEnums	? CalculateSize(GetEnumSchemas(Batch).Last())
										: CalculateSize(GetStructSchemas(Batch).Last());
	return FMemoryView(BatchPtr + Offsets[0], Offsets.Last() + LastSchemaSize - Offsets[0]);
}

} // namespace PlainProps
