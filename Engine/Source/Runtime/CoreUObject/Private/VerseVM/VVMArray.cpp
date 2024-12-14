// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_VERSE_VM || defined(__INTELLISENSE__)
#include "VerseVM/VVMArray.h"
#include "VerseVM/Inline/VVMAbstractVisitorInline.h"
#include "VerseVM/Inline/VVMArrayBaseInline.h"
#include "VerseVM/Inline/VVMCellInline.h"
#include "VerseVM/Inline/VVMMarkStackVisitorInline.h"
#include "VerseVM/VVMCppClassInfo.h"

namespace Verse
{
DEFINE_DERIVED_VCPPCLASSINFO(VTypeArray)
DEFINE_TRIVIAL_VISIT_REFERENCES(VTypeArray);
TGlobalTrivialEmergentTypePtr<&VTypeArray::StaticCppClassInfo> VTypeArray::GlobalTrivialEmergentType;

DEFINE_DERIVED_VCPPCLASSINFO(VArray);
DEFINE_TRIVIAL_VISIT_REFERENCES(VArray);

VArray& VArray::Concat(FRunningContext Context, VArrayBase& Lhs, VArrayBase& Rhs)
{
	EArrayType NewArrayType = DetermineCombinedType(Lhs.GetArrayType(), Rhs.GetArrayType());
	VArray& NewArray = VArray::New(Context, Lhs.Num() + Rhs.Num(), NewArrayType);
	if (NewArrayType != EArrayType::VValue)
	{
		FMemory::Memcpy(NewArray.GetData(), Lhs.GetData(), Lhs.ByteLength());
		switch (NewArrayType)
		{
			case EArrayType::Int32:
				FMemory::Memcpy(NewArray.GetData<int32>() + Lhs.Num(), Rhs.GetData(), Rhs.ByteLength());
				break;
			case EArrayType::Char8:
				FMemory::Memcpy(NewArray.GetData<UTF8CHAR>() + Lhs.Num(), Rhs.GetData(), Rhs.ByteLength());
				break;
			case EArrayType::Char32:
				FMemory::Memcpy(NewArray.GetData<UTF32CHAR>() + Lhs.Num(), Rhs.GetData(), Rhs.ByteLength());
				break;
		}
		return NewArray;
	}

	uint32 Index = 0;
	for (int I = 0; I < Lhs.Num(); ++I)
	{
		NewArray.SetVValue(Context, Index++, Lhs.GetValue(I));
	}
	for (int J = 0; J < Rhs.Num(); ++J)
	{
		NewArray.SetVValue(Context, Index++, Rhs.GetValue(J));
	}
	return NewArray;
}

} // namespace Verse
#endif // WITH_VERSE_VM || defined(__INTELLISENSE__)
