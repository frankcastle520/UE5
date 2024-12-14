// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_VERSE_VM || defined(__INTELLISENSE__)

#include "VerseVM/Inline/VVMValueInline.h"
#include "VerseVM/VVMArray.h"
#include "VerseVM/VVMArrayBase.h"
#include "VerseVM/VVMInt.h"
#include "VerseVM/VVMMarkStackVisitor.h"
#include "VerseVM/VVMMutableArray.h"
#include "VerseVM/VVMTransaction.h"

namespace Verse
{

inline bool VArrayBase::IsInBounds(uint32 Index) const
{
	return Index < Num();
}

inline bool VArrayBase::IsInBounds(const VInt& Index, const uint32 Bounds) const
{
	if (Index.IsInt64())
	{
		const int64 IndexInt64 = Index.AsInt64();
		return (IndexInt64 >= 0) && (IndexInt64 < Bounds);
	}
	else
	{
		// Array maximum size is limited to the maximum size of a unsigned 32-bit integer.
		// So even if it's a `VHeapInt`, if it fails the `IsInt64` check, it is definitely out-of-bounds.
		return false;
	}
}

inline VValue VArrayBase::GetValue(uint32 Index)
{
	checkSlow(IsInBounds(Index));
	switch (GetArrayType())
	{
		case EArrayType::VValue:
			return GetData<VValue>()[Index].Follow();
		case EArrayType::Int32:
			return VValue::FromInt32(GetData<int32>()[Index]);
		case EArrayType::Char8:
			return VValue::Char(GetData<UTF8CHAR>()[Index]);
		case EArrayType::Char32:
			return VValue::Char32(GetData<UTF32CHAR>()[Index]);
		default:
			V_DIE("Unhandled EArrayType encountered!");
	}
}

inline const VValue VArrayBase::GetValue(uint32 Index) const
{
	checkSlow(IsInBounds(Index));
	switch (GetArrayType())
	{
		case EArrayType::VValue:
			return GetData<VValue>()[Index].Follow();
		case EArrayType::Int32:
			return VValue::FromInt32(GetData<int32>()[Index]);
		case EArrayType::Char8:
			return VValue::Char(GetData<UTF8CHAR>()[Index]);
		case EArrayType::Char32:
			return VValue::Char32(GetData<UTF32CHAR>()[Index]);
		default:
			V_DIE("Unhandled EArrayType encountered!");
	}
}

template <bool bTransactional>
inline void VArrayBase::ConvertDataToVValues(FAllocationContext Context, uint32 NewCapacity)
{
	if (GetArrayType() != EArrayType::VValue)
	{
		uint32 Num = this->Num();
		VBuffer NewBuffer = VBuffer(Context, Num, NewCapacity, EArrayType::VValue);
		for (uint32 Index = 0; Index < Num; ++Index)
		{
			new (&NewBuffer.GetData<TWriteBarrier<VValue>>()[Index]) TWriteBarrier<VValue>(Context, GetValue(Index));
		}

		// We need to see the store to ArrayType/Num/all the VValues before the GC
		// sees the buffer itself.
		SetBufferWithStoreBarrier<bTransactional>(Context, NewBuffer);
	}
}

template <bool bTransactional>
inline void VArrayBase::SetValueImpl(FAllocationContext Context, uint32 Index, VValue Value)
{
	checkSlow(Index < Capacity());
	EArrayType ArrayType = GetArrayType();
	if (ArrayType == EArrayType::VValue)
	{
		SetVValue<bTransactional>(Context, Index, Value);
	}
	else if (ArrayType != DetermineArrayType(Value))
	{
		ConvertDataToVValues<bTransactional>(Context, Capacity());
		SetVValue<bTransactional>(Context, Index, Value);
	}
	else
	{
		auto DoSet = [&] {
			switch (ArrayType)
			{
				case EArrayType::Int32:
					SetInt32(Index, Value.AsInt32());
					break;
				case EArrayType::Char8:
					SetChar(Index, Value.AsChar());
					break;
				case EArrayType::Char32:
					SetChar32(Index, Value.AsChar32());
					break;
				default:
					V_DIE("Unhandled EArrayType encountered!");
			}
		};

		if constexpr (bTransactional)
		{
			Context.CurrentTransaction()->AddAuxRoot(Context, Buffer.Get());
			(void)AutoRTFM::Close(DoSet);
		}
		else
		{
			DoSet();
		}
	}
}

inline void VArrayBase::SetValue(FAllocationContext Context, uint32 Index, VValue Value)
{
	SetValueImpl<false>(Context, Index, Value);
}

inline void VArrayBase::SetValueTransactionally(FAllocationContext Context, uint32 Index, VValue Value)
{
	SetValueImpl<true>(Context, Index, Value);
}

template <typename T>
void VArrayBase::Serialize(T*& This, FAllocationContext Context, FAbstractVisitor& Visitor)
{
	if (Visitor.IsLoading())
	{
		std::underlying_type_t<EArrayType> ScratchArrayType;
		Visitor.Visit(ScratchArrayType, TEXT("ArrayType"));
		EArrayType ArrayType = static_cast<EArrayType>(ScratchArrayType);

		uint64 ScratchNumValues = 0;
		if (ArrayType != EArrayType::VValue)
		{
			Visitor.Visit(ScratchNumValues, TEXT("NumValues"));
			This = &T::New(Context, (uint32)ScratchNumValues, ArrayType);
			Visitor.VisitBulkData(This->GetData(), This->ByteLength(), TEXT("Values"));
		}
		else
		{
			Visitor.BeginArray(TEXT("Values"), ScratchNumValues);
			This = &T::New(Context, (uint32)ScratchNumValues, ArrayType);
			Visitor.Visit(This->template GetData<TWriteBarrier<VValue>>(), This->template GetData<TWriteBarrier<VValue>>() + ScratchNumValues);
			Visitor.EndArray();
		}
	}
	else
	{
		EArrayType ArrayType = This->GetArrayType();
		EArrayType SerializedArrayType = ArrayType;
		if (!This->Num())
		{
			SerializedArrayType = EArrayType::None;
		}

		std::underlying_type_t<EArrayType> ScratchArrayType = static_cast<std::underlying_type_t<EArrayType>>(SerializedArrayType);
		Visitor.Visit(ScratchArrayType, TEXT("ArrayType"));

		uint64 ScratchNumValues = This->Num();
		if (ArrayType != EArrayType::VValue)
		{
			Visitor.Visit(ScratchNumValues, TEXT("NumValues"));
			Visitor.VisitBulkData(This->GetData(), This->ByteLength(), TEXT("Values"));
		}
		else
		{
			Visitor.BeginArray(TEXT("Values"), ScratchNumValues);
			Visitor.Visit(This->template GetData<TWriteBarrier<VValue>>(), This->template GetData<TWriteBarrier<VValue>>() + ScratchNumValues);
			Visitor.EndArray();
		}
	}
}

template <typename TVisitor>
inline void VArrayBase::VisitReferencesImpl(TVisitor& Visitor)
{
	VBuffer ThisBuffer = Buffer.Get();
	Visitor.VisitAux(ThisBuffer.GetPtr(), TEXT("ValuesBuffer")); // Visit the buffer we allocated for the array as Aux memory

	if constexpr (TVisitor::bIsAbstractVisitor)
	{
		uint64 ScratchNumValues = ThisBuffer.Num();
		switch (ThisBuffer.GetArrayType())
		{
			case EArrayType::None:
			{
				// Empty-Untyped VMutableArray
				Visitor.BeginArray(TEXT("Values"), ScratchNumValues);
				Visitor.EndArray();
				break;
			}
			case EArrayType::VValue:
			{
				Visitor.BeginArray(TEXT("Values"), ScratchNumValues);
				Visitor.Visit(ThisBuffer.GetData<TWriteBarrier<VValue>>(), ThisBuffer.GetData<TWriteBarrier<VValue>>() + ThisBuffer.Num());
				Visitor.EndArray();
				break;
			}
			case EArrayType::Int32:
			{
				Visitor.BeginArray(TEXT("Values"), ScratchNumValues);
				Visitor.Visit(ThisBuffer.GetData<int32>(), ThisBuffer.GetData<int32>() + ThisBuffer.Num());
				Visitor.EndArray();
				break;
			}
			case EArrayType::Char8:
			{
				Visitor.BeginString(TEXT("Values"), ScratchNumValues);
				Visitor.Visit(ThisBuffer.GetData<uint8>(), ThisBuffer.GetData<uint8>() + ThisBuffer.Num());
				Visitor.EndString();
				break;
			}
			case EArrayType::Char32:
			{
				Visitor.BeginString(TEXT("Values"), ScratchNumValues);
				Visitor.Visit(ThisBuffer.GetData<uint32>(), ThisBuffer.GetData<uint32>() + ThisBuffer.Num());
				Visitor.EndString();
				break;
			}
			default:
				V_DIE("Unhandled EArrayType encountered!");
		}
	}
	else if (ThisBuffer.GetArrayType() == EArrayType::VValue) // Check if we contain elements requiring marking
	{
		// This can race with the mutator while the mutator is growing the array.
		// The reason we don't read garbage VValues is that the mutator will fence
		// between storing the new Value and incrementing Num. So the GC is guaranteed
		// to see the new VValue before it sees the new Num. Therefore, the array the
		// GC sees here is guaranteed to have non-garbage VValues from 0..Num.
		//
		// It's also OK if the GC misses VValues that the mutator adds because the
		// mutator will barrier those new VValues.
		//
		// TODO: In the future we need to support concurrently shrinking arrays.
		// This will happen in the future for two reasons:
		// - STM rollback.
		// - We'll eventually add Verse stdlib APIs that allow elements to be removed from arrays.
		Visitor.Visit(ThisBuffer.GetData<TWriteBarrier<VValue>>(), ThisBuffer.GetData<TWriteBarrier<VValue>>() + ThisBuffer.Num());
	}
}

} // namespace Verse
#endif // WITH_VERSE_VM
