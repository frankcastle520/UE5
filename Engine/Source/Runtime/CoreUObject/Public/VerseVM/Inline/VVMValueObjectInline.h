// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#if WITH_VERSE_VM || defined(__INTELLISENSE__)

#include "VerseVM/Inline/VVMObjectInline.h"
#include "VerseVM/Inline/VVMShapeInline.h"
#include "VerseVM/VVMValueObject.h"

namespace Verse
{

inline VValueObject& VValueObject::NewUninitialized(FAllocationContext Context, VEmergentType& InEmergentType)
{
	return *new (AllocateCell(Context, InEmergentType)) VValueObject(Context, InEmergentType);
}

inline std::byte* VValueObject::AllocateCell(FAllocationContext Context, VEmergentType& InEmergentType)
{
	const uint64 NumIndexedFields = InEmergentType.Shape->NumIndexedFields;
	return Context.AllocateFastCell(DataOffset(*InEmergentType.CppClassInfo) + NumIndexedFields * sizeof(VRestValue));
}

inline VValueObject::VValueObject(FAllocationContext Context, VEmergentType& InEmergentType)
	: VObject(Context, InEmergentType)
{
	// We only need to allocate space for indexed fields since we are raising constants to the shape
	// and not storing their data on per-object instances.
	VRestValue* Data = GetFieldData(*InEmergentType.CppClassInfo);
	const uint64 NumIndexedFields = InEmergentType.Shape->NumIndexedFields;
	for (uint64 Index = 0; Index < NumIndexedFields; ++Index)
	{
		// TODO SOL-4222: Pipe through proper split depth here.
		new (&Data[Index]) VRestValue(0);
	}
}

} // namespace Verse
#endif // WITH_VERSE_VM
