// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_VERSE_VM || defined(__INTELLISENSE__)
#include "VerseVM/VVMMutableArray.h"
#include "VerseVM/Inline/VVMAbstractVisitorInline.h"
#include "VerseVM/Inline/VVMArrayBaseInline.h"
#include "VerseVM/Inline/VVMCellInline.h"
#include "VerseVM/Inline/VVMMarkStackVisitorInline.h"
#include "VerseVM/Inline/VVMMutableArrayInline.h"
#include "VerseVM/Inline/VVMValueInline.h"
#include "VerseVM/VVMCppClassInfo.h"
#include "VerseVM/VVMOpResult.h"

namespace Verse
{
DEFINE_DERIVED_VCPPCLASSINFO(VMutableArray);
DEFINE_TRIVIAL_VISIT_REFERENCES(VMutableArray);
TGlobalTrivialEmergentTypePtr<&VMutableArray::StaticCppClassInfo> VMutableArray::GlobalTrivialEmergentType;

void VMutableArray::Reset(FAllocationContext Context)
{
	SetBufferWithStoreBarrier(Context, VBuffer());
}

void VMutableArray::Append(FAllocationContext Context, VArrayBase& Array)
{
	if (!Buffer && Array.Num())
	{
		uint32 Num = 0;
		uint32 Capacity = Array.Num();
		VBuffer NewBuffer = VBuffer(Context, Num, Capacity, Array.GetArrayType());
		// We barrier because the GC needs to see the store to ArrayType/Num if
		// it sees the new buffer.
		SetBufferWithStoreBarrier(Context, NewBuffer);
	}
	else if (GetArrayType() != EArrayType::VValue && GetArrayType() != Array.GetArrayType())
	{
		ConvertDataToVValues(Context, Num() + Array.Num());
	}

	switch (GetArrayType())
	{
		case EArrayType::None:
			V_DIE_UNLESS(Array.GetArrayType() == EArrayType::None);
			// Empty-Untyped VMutableArray appending Empty-Untyped VMutableArray
			break;
		case EArrayType::VValue:
			Append<TWriteBarrier<VValue>>(Context, Array);
			break;
		case EArrayType::Int32:
			Append<int32>(Context, Array);
			break;
		case EArrayType::Char8:
			Append<UTF8CHAR>(Context, Array);
			break;
		case EArrayType::Char32:
			Append<UTF32CHAR>(Context, Array);
			break;
		default:
			V_DIE("Unhandled EArrayType encountered!");
	}
}

VValue VMutableArray::FreezeImpl(FAllocationContext Context)
{
	EArrayType ArrayType = GetArrayType();
	VArray& FrozenArray = VArray::New(Context, Num(), ArrayType);
	if (ArrayType != EArrayType::VValue)
	{
		FMemory::Memcpy(FrozenArray.GetData(), GetData(), ByteLength());
	}
	else
	{
		for (uint32 I = 0; I < Num(); ++I)
		{
			FrozenArray.SetValue(Context, I, VValue::Freeze(Context, GetValue(I)));
		}
	}
	return FrozenArray;
}

} // namespace Verse
#endif // WITH_VERSE_VM || defined(__INTELLISENSE__)
