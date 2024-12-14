// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#if WITH_VERSE_VM || defined(__INTELLISENSE__)

#include "UObject/VerseValueProperty.h"
#include "VerseVM/VVMCell.h"
#include "VerseVM/VVMGlobalTrivialEmergentTypePtr.h"
#include "VerseVM/VVMNativeStruct.h"

namespace Verse
{

struct VNativeRef : VCell
{
	DECLARE_DERIVED_VCPPCLASSINFO(COREUOBJECT_API, VCell);
	COREUOBJECT_API static TGlobalTrivialEmergentTypePtr<&StaticCppClassInfo> GlobalTrivialEmergentType;

	enum class EType : uint8
	{
		FProperty,
	};

	// The source this ref was projected from. Either a UObject or a VNativeStruct.
	TWriteBarrier<VValue> Base;

	union
	{
		FProperty* UProperty;
	};

	EType Type;

	COREUOBJECT_API VValue Get(FAllocationContext Context);

	COREUOBJECT_API static VValue Get(FAllocationContext Context, void* Container, FProperty* Property);

	COREUOBJECT_API FOpResult Set(FAllocationContext Context, VValue Value);

	COREUOBJECT_API FOpResult SetNonTransactionally(FAllocationContext Context, VValue Value);

	template <bool bTransactional, typename BaseType>
	static FOpResult Set(FAllocationContext Context, BaseType Base, void* Container, FProperty* Property, VValue Value);

	static VNativeRef& New(FAllocationContext Context, UObject* Base, FProperty* Property)
	{
		return *new (Context.AllocateFastCell(sizeof(VNativeRef))) VNativeRef(Context, Base, Property);
	}

	static VNativeRef& New(FAllocationContext Context, VNativeStruct* Base, FProperty* Property)
	{
		return *new (Context.AllocateFastCell(sizeof(VNativeRef))) VNativeRef(Context, *Base, Property);
	}

	COREUOBJECT_API VValue FreezeImpl(FAllocationContext Context);

private:
	VNativeRef(FAllocationContext Context, VValue InBase, FProperty* InProperty)
		: VCell(Context, &GlobalTrivialEmergentType.Get(Context))
		, Base(Context, InBase)
		, UProperty(InProperty)
		, Type(EType::FProperty)
	{
		V_DIE_IF(InProperty->IsA<FVRestValueProperty>());
		SetIsDeeplyMutable();
	}
};

extern template FOpResult VNativeRef::Set<true>(FAllocationContext Context, UObject* Base, void* Container, FProperty* Property, VValue Value);
extern template FOpResult VNativeRef::Set<true>(FAllocationContext Context, VNativeStruct* Base, void* Container, FProperty* Property, VValue Value);
extern template FOpResult VNativeRef::Set<false>(FAllocationContext Context, std::nullptr_t Base, void* Container, FProperty* Property, VValue Value);

} // namespace Verse
#endif
