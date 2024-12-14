// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_VERSE_VM || defined(__INTELLISENSE__)
#include "VerseVM/VVMNativeStruct.h"
#include "Templates/TypeCompatibleBytes.h"
#include "Templates/TypeHash.h"
#include "UObject/UnrealType.h"
#include "UObject/VerseValueProperty.h"
#include "VerseVM/Inline/VVMClassInline.h"
#include "VerseVM/Inline/VVMMarkStackVisitorInline.h"
#include "VerseVM/Inline/VVMNativeStructInline.h"
#include "VerseVM/Inline/VVMShapeInline.h"
#include "VerseVM/VVMCppClassInfo.h"
#include "VerseVM/VVMEmergentTypeCreator.h"

namespace Verse
{
DEFINE_DERIVED_VCPPCLASSINFO(VNativeStruct);

template <typename TVisitor>
void VNativeStruct::VisitReferencesImpl(TVisitor& Visitor)
{
	const VEmergentType* EmergentType = GetEmergentType();

	// Visit the portion of this struct that is known to Verse
	void* Data = GetData(*EmergentType->CppClassInfo);
	for (auto It = EmergentType->Shape->CreateFieldsIterator(); It; ++It)
	{
		switch (It->Value.Type)
		{
			case EFieldType::FProperty:
				// C++ is responsible for tracing native fields.
				break;
			case EFieldType::FVerseProperty:
				::Verse::Visit(Visitor, *It->Value.UProperty->ContainerPtrToValuePtr<VRestValue>(Data), *WriteToString<64>(It->Key->AsStringView()));
				break;
			case EFieldType::Offset:
			case EFieldType::FPropertyVar:
			case EFieldType::Constant:
				VERSE_UNREACHABLE();
				break;
		}
	}
}

VNativeStruct& VNativeStruct::Duplicate(FAllocationContext Context)
{
	VEmergentType* EmergentType = GetEmergentType();
	UScriptStruct::ICppStructOps* CppStructOps = GetUScriptStruct(*EmergentType)->GetCppStructOps();
	const bool bPlainOldData = CppStructOps->IsPlainOldData();
	VNativeStruct& NewObject = VNativeStruct::NewUninitialized(Context, *EmergentType, !bPlainOldData);
	void* Data = GetData(*EmergentType->CppClassInfo);
	void* NewData = NewObject.GetData(*EmergentType->CppClassInfo);

	if (bPlainOldData)
	{
		memcpy(NewData, Data, CppStructOps->GetSize());
	}
	else
	{
		// TODO: AutoRTFM::Close and propagate any errors.
		CppStructOps->Copy(NewData, Data, 1);
	}

	return NewObject;
}

bool VNativeStruct::EqualImpl(FAllocationContext Context, VCell* Other, const TFunction<void(::Verse::VValue, ::Verse::VValue)>& HandlePlaceholder)
{
	// Since native structs carry blind C++ data, they can only be compared to the exact same type
	VEmergentType* EmergentType = GetEmergentType();
	if (EmergentType != Other->GetEmergentType())
	{
		return false;
	}

	// Trust the C++ equality operator to do the right thing
	UScriptStruct::ICppStructOps* CppStructOps = GetUScriptStruct(*EmergentType)->GetCppStructOps();
	V_DIE_UNLESS(CppStructOps->HasIdentical());
	VNativeStruct& OtherStruct = Other->StaticCast<VNativeStruct>();

	// TODO: AutoRTFM::Close and propagate any errors.
	bool bResult = false;
	CppStructOps->Identical(GetData(*EmergentType->CppClassInfo), OtherStruct.GetData(*EmergentType->CppClassInfo), PPF_None, bResult);
	return bResult;
}

// TODO: Make this (And all other container TypeHash funcs) handle placeholders appropriately
uint32 VNativeStruct::GetTypeHashImpl()
{
	VEmergentType* EmergentType = GetEmergentType();
	UScriptStruct::ICppStructOps* CppStructOps = GetUScriptStruct(*EmergentType)->GetCppStructOps();
	V_DIE_UNLESS(CppStructOps->HasGetTypeHash());

	// TODO: AutoRTFM::Close and propagate any errors.
	return CppStructOps->GetStructTypeHash(GetData(*EmergentType->CppClassInfo));
}

VValue VNativeStruct::MeltImpl(FAllocationContext Context)
{
	// First make a native copy, then run the melt process on top of that
	VNativeStruct& NewObject = Duplicate(Context);

	// Now, do a second pass where we individually melt each VValue
	VEmergentType* EmergentType = GetEmergentType();
	void* Data = GetData(*EmergentType->CppClassInfo);
	void* NewData = NewObject.GetData(*EmergentType->CppClassInfo);
	for (auto It = EmergentType->Shape->CreateFieldsIterator(); It; ++It)
	{
		VValue MeltResult;
		switch (It->Value.Type)
		{
			case EFieldType::FProperty:
				// C++ copy constructor is responsible for melting native fields.
				break;
			case EFieldType::FVerseProperty:
				MeltResult = VValue::Melt(Context, It->Value.UProperty->ContainerPtrToValuePtr<VRestValue>(Data)->Get(Context));
				if (MeltResult.IsPlaceholder())
				{
					return MeltResult;
				}
				It->Value.UProperty->ContainerPtrToValuePtr<VRestValue>(NewData)->Set(Context, MeltResult);
				break;
			case EFieldType::Offset:
			case EFieldType::FPropertyVar:
			case EFieldType::Constant:
				VERSE_UNREACHABLE();
				break;
		}
	}

	return VValue(NewObject);
}

VValue VNativeStruct::FreezeImpl(FAllocationContext Context)
{
	// First make a native copy, then run the freeze process on top of that
	VNativeStruct& NewObject = Duplicate(Context);

	// Now, do a second pass where we individually freeze each VValue
	VEmergentType* EmergentType = GetEmergentType();
	void* Data = GetData(*EmergentType->CppClassInfo);
	void* NewData = NewObject.GetData(*EmergentType->CppClassInfo);
	for (auto It = EmergentType->Shape->CreateFieldsIterator(); It; ++It)
	{
		VValue FreezeResult;
		switch (It->Value.Type)
		{
			case EFieldType::FProperty:
				// C++ copy constructor is responsible for freezing native fields.
				break;
			case EFieldType::FVerseProperty:
				FreezeResult = VValue::Freeze(Context, It->Value.UProperty->ContainerPtrToValuePtr<VRestValue>(Data)->Get(Context));
				It->Value.UProperty->ContainerPtrToValuePtr<VRestValue>(NewData)->Set(Context, FreezeResult);
				break;
			case EFieldType::Offset:
			case EFieldType::FPropertyVar:
			case EFieldType::Constant:
				VERSE_UNREACHABLE();
				break;
		}
	}

	return VValue(NewObject);
}

} // namespace Verse
#endif // WITH_VERSE_VM || defined(__INTELLISENSE__)
