// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#if WITH_VERSE_VM || defined(__INTELLISENSE__)

#include "VerseVM/VVMObject.h"

namespace Verse
{
struct VClass;

template <typename CppStructType>
VClass& StaticVClass();

/// A variant of Verse object that boxes a native (C++ defined) struct
struct VNativeStruct : VObject
{
	DECLARE_DERIVED_VCPPCLASSINFO(COREUOBJECT_API, VObject);

	template <class CppStructType>
	CppStructType& GetStruct();
	void* GetStruct();

	/// Allocate a new VNativeStruct and move an existing struct into it
	template <class CppStructType>
	static VNativeStruct& New(FAllocationContext Context, VEmergentType& InEmergentType, CppStructType&& InStruct);

	/// Allocate a new blank VNativeStruct
	static VNativeStruct& NewUninitialized(FAllocationContext Context, VEmergentType& InEmergentType, bool bRunCppConstructor = true);

protected:
	friend class FInterpreter;
	friend struct VNativeRef;

	static std::byte* AllocateCell(FAllocationContext Context, VEmergentType& EmergentType);

	static UScriptStruct* GetUScriptStruct(VEmergentType& EmergentType);

	template <class CppStructType>
	VNativeStruct(FAllocationContext Context, VEmergentType& InEmergentType, CppStructType&& InStruct);
	VNativeStruct(FAllocationContext Context, VEmergentType& InEmergentType, bool bRunCppConstructor);
	~VNativeStruct();

	VNativeStruct& Duplicate(FAllocationContext Context);

	COREUOBJECT_API bool EqualImpl(FAllocationContext Context, VCell* Other, const TFunction<void(::Verse::VValue, ::Verse::VValue)>& HandlePlaceholder);
	COREUOBJECT_API uint32 GetTypeHashImpl();
	COREUOBJECT_API VValue MeltImpl(FAllocationContext Context);
	COREUOBJECT_API VValue FreezeImpl(FAllocationContext Context);
};

} // namespace Verse
#endif // WITH_VERSE_VM
