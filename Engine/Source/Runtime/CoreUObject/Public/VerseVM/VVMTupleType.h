// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#if WITH_VERSE_VM || defined(__INTELLISENSE__)

#include "VerseVM/VVMArray.h"
#include "VerseVM/VVMGlobalTrivialEmergentTypePtr.h"
#include "VerseVM/VVMType.h"
#include "VerseVM/VVMVerseStruct.h"

class UVerseStruct;

namespace Verse
{

struct VPackage;
struct VPropertyType;

struct VTupleType : VType
{
	DECLARE_DERIVED_VCPPCLASSINFO(COREUOBJECT_API, VType);
	COREUOBJECT_API static TGlobalTrivialEmergentTypePtr<&StaticCppClassInfo> GlobalTrivialEmergentType;

	VArray& GetUEMangledName() const { return *UEMangledName.Get(); }
	UVerseStruct* GetOrCreateUStruct(FAllocationContext Context, VPackage* Scope);

	static VTupleType& New(FAllocationContext Context, FUtf8StringView UEMangledName, TArrayView<VPropertyType*> ElementTypes)
	{
		return *new (Context.Allocate(FHeap::DestructorSpace, sizeof(VTupleType) + ElementTypes.Num() * sizeof(TWriteBarrier<VPropertyType>))) VTupleType(Context, UEMangledName, ElementTypes);
	}

	uint32 NumElements;
	TWriteBarrier<VPropertyType>* GetElementTypes() { return (TWriteBarrier<VPropertyType>*)((char*)this + sizeof(*this)); }
	const TWriteBarrier<VPropertyType>* GetElementTypes() const { return (TWriteBarrier<VPropertyType>*)((const char*)this + sizeof(*this)); }

private:
	VTupleType(FAllocationContext Context, FUtf8StringView InUEMangledName, TArrayView<VPropertyType*> ElementTypes)
		: VType(Context, &GlobalTrivialEmergentType.Get(Context))
		, NumElements(ElementTypes.Num())
		, UEMangledName(Context, &VArray::New(Context, InUEMangledName))
	{
		TWriteBarrier<VPropertyType>* ElementStorage = GetElementTypes();
		for (uint32 Index = 0; Index < NumElements; ++Index)
		{
			new (&ElementStorage[Index]) TWriteBarrier<VPropertyType>(Context, ElementTypes[Index]);
		}
	}

	COREUOBJECT_API void CreateUStruct(FAllocationContext Context, VPackage* Scope, TWriteBarrier<VValue>& Result);

	TWriteBarrier<VArray> UEMangledName;

	struct FUStructMapKeyFuncs : TDefaultMapKeyFuncs<TWriteBarrier<VPackage>, TWriteBarrier<VValue>, /*bInAllowDuplicateKeys*/ false>
	{
		static bool Matches(KeyInitType A, KeyInitType B) { return A == B; }
		static bool Matches(KeyInitType A, VPackage* B) { return A.Get() == B; }
		static uint32 GetKeyHash(KeyInitType Key) { return ::PointerHash(Key.Get()); }
		static uint32 GetKeyHash(VPackage* Key) { return ::PointerHash(Key); }
	};
	TMap<TWriteBarrier<VPackage>, TWriteBarrier<VValue>, FDefaultSetAllocator, FUStructMapKeyFuncs> AssociatedUStructs;
};

inline UVerseStruct* VTupleType::GetOrCreateUStruct(FAllocationContext Context, VPackage* Scope)
{
	TWriteBarrier<VValue>& Entry = AssociatedUStructs.FindOrAdd({Context, Scope});
	if (!Entry)
	{
		CreateUStruct(Context, Scope, Entry);
	}
	return CastChecked<UVerseStruct>(Entry.Get().AsUObject());
}

} // namespace Verse
#endif // WITH_VERSE_VM
