// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_VERSE_VM || defined(__INTELLISENSE__)
#include "VerseVM/VVMArrayBase.h"
#include "Templates/TypeHash.h"
#include "VerseVM/Inline/VVMAbstractVisitorInline.h"
#include "VerseVM/Inline/VVMArrayBaseInline.h"
#include "VerseVM/Inline/VVMEqualInline.h"
#include "VerseVM/Inline/VVMMarkStackVisitorInline.h"
#include "VerseVM/Inline/VVMValueInline.h"
#include "VerseVM/VVMCppClassInfo.h"
#include "VerseVM/VVMMutableArray.h"
#include "VerseVM/VVMOpResult.h"

namespace Verse
{
DEFINE_DERIVED_VCPPCLASSINFO(VArrayBase);

bool VArrayBase::EqualImpl(FAllocationContext Context, VCell* Other, const TFunction<void(::Verse::VValue, ::Verse::VValue)>& HandlePlaceholder)
{
	if (!Other->IsA<VArrayBase>())
	{
		return false;
	}

	VArrayBase& OtherArray = Other->StaticCast<VArrayBase>();
	if (Num() != OtherArray.Num())
	{
		return false;
	}

	if (DetermineCombinedType(GetArrayType(), OtherArray.GetArrayType()) != EArrayType::VValue)
	{
		return FMemory::Memcmp(GetData(), OtherArray.GetData(), ByteLength()) == 0;
	}
	else
	{
		for (uint32 Index = 0, End = Num(); Index < End; ++Index)
		{
			if (!VValue::Equal(Context, GetValue(Index), OtherArray.GetValue(Index), HandlePlaceholder))
			{
				return false;
			}
		}
	}
	return true;
}

VValue VArrayBase::MeltImpl(FAllocationContext Context)
{
	EArrayType ArrayType = GetArrayType();
	if (ArrayType != EArrayType::VValue)
	{
		VMutableArray& MeltedArray = VMutableArray::New(Context, Num(), Num(), ArrayType);
		FMemory::Memcpy(MeltedArray.GetData(), GetData(), ByteLength());
		return MeltedArray;
	}

	VMutableArray& MeltedArray = VMutableArray::New(Context, 0, Num(), EArrayType::VValue);
	for (uint32 I = 0; I < Num(); ++I)
	{
		VValue Result = VValue::Melt(Context, GetValue(I));
		if (Result.IsPlaceholder())
		{
			return Result;
		}
		MeltedArray.AddValue(Context, Result);
	}
	return MeltedArray;
}

uint32 VArrayBase::GetTypeHashImpl()
{
	switch (GetArrayType())
	{
		case EArrayType::None:
			return 0; // Empty-Untyped VMutableArray
		case EArrayType::VValue:
			return ::GetArrayHash(GetData<TWriteBarrier<VValue>>(), Num());
		case EArrayType::Int32:
			return ::GetArrayHash(GetData<int32>(), Num());
		case EArrayType::Char8:
			return ::GetArrayHash(GetData<UTF8CHAR>(), Num());
		case EArrayType::Char32:
			return ::GetArrayHash(GetData<UTF32CHAR>(), Num());
		default:
			V_DIE("Unhandled EArrayType encountered!");
	}
}

void VArrayBase::ToStringImpl(FStringBuilderBase& Builder, FAllocationContext Context, const FCellFormatter& Formatter)
{
	// We print UTF8 Arrays as strings for ease of reading when debugging and logging.
	if (IsString())
	{
		Builder.Append(FString::Printf(TEXT("\"%s\""), *AsString()));
		return;
	}

	for (uint32 I = 0; I < Num(); ++I)
	{
		if (I > 0)
		{
			Builder.Append(TEXT(", "));
		}
		GetValue(I).ToString(Builder, Context, Formatter);
	}
}

VArrayBase::FConstIterator VArrayBase::begin() const
{
	switch (GetArrayType())
	{
		case EArrayType::None:
			return GetData(); // Empty-Untyped VMutableArray
		case EArrayType::VValue:
			return GetData<TWriteBarrier<VValue>>();
		case EArrayType::Int32:
			return GetData<int32>();
		case EArrayType::Char8:
			return GetData<UTF8CHAR>();
		case EArrayType::Char32:
			return GetData<UTF32CHAR>();
		default:
			V_DIE("Unhandled EArrayType encountered!");
	}
}

VArrayBase::FConstIterator VArrayBase::end() const
{
	switch (GetArrayType())
	{
		case EArrayType::None:
			return GetData(); // Empty-Untyped VMutableArray
		case EArrayType::VValue:
			return GetData<TWriteBarrier<VValue>>() + Num();
		case EArrayType::Int32:
			return GetData<int32>() + Num();
		case EArrayType::Char8:
			return GetData<UTF8CHAR>() + Num();
		case EArrayType::Char32:
			return GetData<UTF32CHAR>() + Num();
		default:
			V_DIE("Unhandled EArrayType encountered!");
	}
}

} // namespace Verse
#endif // WITH_VERSE_VM || defined(__INTELLISENSE__)
