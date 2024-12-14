// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#if WITH_VERSE_VM || defined(__INTELLISENSE__)

#include "VerseVM/VVMObject.h"

namespace Verse
{

/// Specialization of VObject that stores only VValues
struct VValueObject : VObject
{
	DECLARE_DERIVED_VCPPCLASSINFO(COREUOBJECT_API, VObject);

	/// Allocate a new object with the given shape, populated with placeholders
	static VValueObject& NewUninitialized(FAllocationContext Context, VEmergentType& InEmergentType);

protected:
	friend class FInterpreter;

	static std::byte* AllocateCell(FAllocationContext Context, VEmergentType& EmergentType);

	VValueObject(FAllocationContext Context, VEmergentType& InEmergentType);

	COREUOBJECT_API bool EqualImpl(FAllocationContext Context, VCell* Other, const TFunction<void(::Verse::VValue, ::Verse::VValue)>& HandlePlaceholder);
	COREUOBJECT_API uint32 GetTypeHashImpl();
	COREUOBJECT_API VValue MeltImpl(FAllocationContext Context);
	COREUOBJECT_API VValue FreezeImpl(FAllocationContext Context);
};
} // namespace Verse
#endif // WITH_VERSE_VM
