// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PlainPropsBuild.h"
#include "Containers/Array.h"
#include "Containers/UnrealString.h"

namespace PlainProps
{

struct FBuiltStruct
{
	~FBuiltStruct() = delete; // Allocated in FScratchAllocator 
	
	uint16				NumMembers;
	FBuiltMember		Members[0];
};

struct FBuiltRange
{
	~FBuiltRange() = delete; // Allocated in FScratchAllocator

	[[nodiscard]] static FBuiltRange*					Create(FScratchAllocator& Allocator, uint64 NumItems, SIZE_T ItemSize);

	uint64												Num;
	uint8												Data[0];
	
	TConstArrayView64<const FBuiltRange*>				AsRanges() const	{ return { reinterpret_cast<FBuiltRange const* const*>(Data),	static_cast<int64>(Num) }; }
	TConstArrayView64<const FBuiltStruct*>				AsStructs() const	{ return { reinterpret_cast<FBuiltStruct const* const*>(Data),	static_cast<int64>(Num) }; }
};

//////////////////////////////////////////////////////////////////////////

inline void WriteData(TArray64<uint8>& Out, const void* Data, int64 Size)
{
	Out.Append(static_cast<const uint8*>(Data), Size);
}

template<typename ArrayType>
void WriteArray(TArray64<uint8>& Out, const ArrayType& In)
{
	static_assert(TIsContiguousContainer<ArrayType>::Value);
	WriteData(Out, In.GetData(), sizeof(typename ArrayType::ElementType) * In.Num());
}

template<typename T>
void WriteAlignmentPadding(TArray64<uint8>& Out)
{
	Out.AddZeroed(Align(Out.Num(), alignof(T)) - Out.Num());
}	

template<typename T>
void WriteAlignedArray(TArray64<uint8>& Out, TArrayView<T> In)
{
	WriteAlignmentPadding<T>(Out);
	WriteArray(Out, In);
}

template<typename T>
inline void WriteInt(TArray64<uint8>& Out, T Number)
{
	static_assert(std::is_integral_v<T>);
	static_assert(PLATFORM_LITTLE_ENDIAN);
	WriteData(Out, &Number, sizeof(T));
}

inline void WriteU32(TArray64<uint8>& Out, uint32 Int) { WriteInt(Out, Int); }
inline void WriteU64(TArray64<uint8>& Out, uint64 Int) { WriteInt(Out, Int); }

PLAINPROPS_API uint64 WriteSkippableSlice(TArray64<uint8>& Out, TConstArrayView64<uint8> Slice);


} // namespace PlainProps