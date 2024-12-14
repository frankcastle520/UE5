// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#if WITH_VERSE_VM || defined(__INTELLISENSE__)

#include "VerseVM/VVMClass.h"
#include "VerseVM/VVMNativeStruct.h"
#include "VerseVM/VVMObject.h"

namespace Verse
{

template <class CppStructType>
inline CppStructType& VNativeStruct::GetStruct()
{
	checkSlow(GetUScriptStruct(*GetEmergentType()) == StaticStruct<CppStructType>());

	return *BitCast<CppStructType*>(GetStruct());
}

inline void* VNativeStruct::GetStruct()
{
	return VObject::GetData(*GetEmergentType()->CppClassInfo);
}

template <class CppStructType>
inline VNativeStruct& VNativeStruct::New(FAllocationContext Context, VEmergentType& InEmergentType, CppStructType&& InStruct)
{
	return *new (AllocateCell(Context, InEmergentType)) VNativeStruct(Context, InEmergentType, Forward<CppStructType>(InStruct));
}

inline VNativeStruct& VNativeStruct::NewUninitialized(FAllocationContext Context, VEmergentType& InEmergentType, bool bRunCppConstructor)
{
	return *new (AllocateCell(Context, InEmergentType)) VNativeStruct(Context, InEmergentType, bRunCppConstructor);
}

inline std::byte* VNativeStruct::AllocateCell(FAllocationContext Context, VEmergentType& InEmergentType)
{
	UScriptStruct::ICppStructOps* CppStructOps = GetUScriptStruct(InEmergentType)->GetCppStructOps();
	const size_t ByteSize = DataOffset(*InEmergentType.CppClassInfo) + CppStructOps->GetSize();
	const bool bHasDestructor = CppStructOps->HasDestructor();
	return bHasDestructor ? Context.Allocate(FHeap::DestructorSpace, ByteSize) : Context.AllocateFastCell(ByteSize);
}

inline UScriptStruct* VNativeStruct::GetUScriptStruct(VEmergentType& EmergentType)
{
	return EmergentType.Type->StaticCast<VClass>().GetUStruct<UScriptStruct>();
}

template <class CppStructType>
inline VNativeStruct::VNativeStruct(FAllocationContext Context, VEmergentType& InEmergentType, CppStructType&& InStruct)
	: VObject(Context, InEmergentType)
{
	using StructType = typename TDecay<CppStructType>::Type;
	checkSlow(sizeof(StructType) == GetUScriptStruct(InEmergentType)->GetCppStructOps()->GetSize());

	SetIsStruct();
	void* Data = GetData(*InEmergentType.CppClassInfo);

	// TODO: AutoRTFM::Close and propagate any errors.
	new (Data) StructType(Forward<CppStructType>(InStruct));
}

inline VNativeStruct::VNativeStruct(FAllocationContext Context, VEmergentType& InEmergentType, bool bRunCppConstructor)
	: VObject(Context, InEmergentType)
{
	SetIsStruct();
	if (bRunCppConstructor)
	{
		UScriptStruct::ICppStructOps* CppStructOps = GetUScriptStruct(InEmergentType)->GetCppStructOps();
		void* Data = GetData(*InEmergentType.CppClassInfo);
		if (CppStructOps->HasZeroConstructor())
		{
			memset(Data, 0, CppStructOps->GetSize());
		}
		else
		{
			// TODO: AutoRTFM::Close and propagate any errors.
			CppStructOps->Construct(Data);
		}
	}
}

inline VNativeStruct::~VNativeStruct()
{
	VEmergentType* EmergentType = GetEmergentType();
	UScriptStruct::ICppStructOps* CppStructOps = GetUScriptStruct(*EmergentType)->GetCppStructOps();
	if (CppStructOps->HasDestructor())
	{
		// TODO: AutoRTFM::Close and propagate any errors.
		CppStructOps->Destruct(VObject::GetData(*EmergentType->CppClassInfo));
	}
}

} // namespace Verse
#endif // WITH_VERSE_VM
