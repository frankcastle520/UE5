// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_VERSE_VM || defined(__INTELLISENSE__)

#include "VerseVM/Inline/VVMArrayBaseInline.h"
#include "VerseVM/Inline/VVMValueInline.h"
#include "VerseVM/VVMInt.h"
#include "VerseVM/VVMMutableArray.h"

namespace Verse
{

inline void VMutableArray::AddValue(FAllocationContext Context, VValue Value)
{
	if (!GetData())
	{
		uint32 Num = 0;
		uint32 Capacity = 4;
		VBuffer NewBuffer = VBuffer(Context, Num, Capacity, DetermineArrayType(Value));
		SetBufferWithStoreBarrier(Context, NewBuffer);
	}
	else if (GetArrayType() != EArrayType::VValue && GetArrayType() != DetermineArrayType(Value))
	{
		uint32 Capacity = this->Capacity();
		if (Num() == Capacity) // Check our capacity before re-allocating as VValues
		{
			Capacity = Capacity * 2;
		}
		ConvertDataToVValues(Context, Capacity);
	}
	else if (Num() == Capacity())
	{
		uint32 NewCapacity = Capacity() * 2;
		VBuffer NewBuffer = VBuffer(Context, Num(), NewCapacity, GetArrayType());
		FMemory::Memcpy(NewBuffer.GetData(), GetData(), ByteLength());
		// We might be copying around a VValue buffer, so we gotta barrier before we expose this buffer to the GC.
		SetBufferWithStoreBarrier(Context, NewBuffer);
	}

	uint32 Index = Num();
	V_DIE_UNLESS(Index < Capacity());
	SetValue(Context, Index, Value);
	// The above store needs to happen before the GC sees an updated NumValues.
	StoreStoreFence();
	++Buffer.Get().GetHeader()->NumValues;
	if (IsString())
	{
		SetNullTerminator();
	}
}

template <typename T>
inline void VMutableArray::Append(FAllocationContext Context, VArrayBase& Array)
{
	checkSlow(GetArrayType() != EArrayType::None && GetArrayType() != EArrayType::VValue && GetArrayType() == Array.GetArrayType());
	const uint32 NewNumValues = Num() + Array.Num();
	uint32 Capacity = this->Capacity();
	if (NewNumValues > Capacity)
	{
		Capacity = NewNumValues * 2;
		VBuffer NewBuffer = VBuffer(Context, NewNumValues, Capacity, GetArrayType());
		FMemory::Memcpy(NewBuffer.GetData(), GetData(), ByteLength());
		// We need the store of the array type in the buffer to happen
		// before the GC sees the new buffer.
		SetBufferWithStoreBarrier(Context, NewBuffer);
	}
	FMemory::Memcpy(GetData<T>() + Num(), Array.GetData<T>(), Array.ByteLength());
	// We don't need to barrier here because the GC doesn't mark primitive arrays.
	Buffer.Get().GetHeader()->NumValues = NewNumValues;
	if (IsString())
	{
		SetNullTerminator();
	}
}

template <>
inline void VMutableArray::Append<TWriteBarrier<VValue>>(FAllocationContext Context, VArrayBase& Array)
{
	checkSlow(GetArrayType() == EArrayType::VValue);
	for (uint32 Index = 0, End = Array.Num(); Index < End; ++Index)
	{
		AddValue(Context, Array.GetValue(Index));
	}
}

inline VMutableArray& VMutableArray::Concat(FAllocationContext Context, VArrayBase& Lhs, VArrayBase& Rhs)
{
	VMutableArray& NewArray = VMutableArray::New(Context, 0, Lhs.Num() + Rhs.Num(), DetermineCombinedType(Lhs.GetArrayType(), Rhs.GetArrayType()));
	NewArray.Append(Context, Lhs);
	NewArray.Append(Context, Rhs);
	return NewArray;
}

} // namespace Verse
#endif // WITH_VERSE_VM
