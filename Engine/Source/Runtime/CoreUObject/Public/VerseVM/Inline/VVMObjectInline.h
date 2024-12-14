// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#if WITH_VERSE_VM || defined(__INTELLISENSE__)

#include "UObject/UnrealType.h" // For FProperty
#include "UObject/VerseValueProperty.h"
#include "VerseVM/Inline/VVMShapeInline.h"
#include "VerseVM/VVMFunction.h"
#include "VerseVM/VVMNativeConverter.h"
#include "VerseVM/VVMNativeFunction.h"
#include "VerseVM/VVMNativeRef.h"
#include "VerseVM/VVMNativeStruct.h"
#include "VerseVM/VVMObject.h"
#include "VerseVM/VVMProcedure.h"
#include "VerseVM/VVMUnreachable.h"
#include "VerseVM/VVMVar.h"

namespace Verse
{

inline VValue VObject::LoadField(FAllocationContext Context, const VCppClassInfo& CppClassInfo, const VShape::VEntry* Field)
{
	V_DIE_IF(Field == nullptr);

	switch (Field->Type)
	{
		case EFieldType::Offset:
			return GetFieldData(CppClassInfo)[Field->Index].Get(Context);
		case EFieldType::FProperty:
			return VNativeRef::Get(Context, GetData(CppClassInfo), Field->UProperty);
		case EFieldType::FPropertyVar:
			return VNativeRef::New(Context, this->DynamicCast<VNativeStruct>(), Field->UProperty);
		case EFieldType::FVerseProperty:
			return Field->UProperty->ContainerPtrToValuePtr<VRestValue>(GetData(CppClassInfo))->Get(Context);
		case EFieldType::Constant:
		{
			VValue FieldValue = Field->Value.Get();
			V_DIE_IF(FieldValue.IsCellOfType<VProcedure>());
			if (VFunction* Function = FieldValue.DynamicCast<VFunction>(); Function && !Function->HasSelf())
			{
				// NOTE: (yiliang.siew) Update the function-without-`Self` to point to the current object instance.
				// We only do this if the function doesn't already have a `Self` bound - in the case of fields that
				// are pointing to functions, we don't want to overwrite that `Self` which was already previously-bound.
				return Function->Bind(Context, *this);
			}
			else if (VNativeFunction* NativeFunction = FieldValue.DynamicCast<VNativeFunction>(); NativeFunction && !NativeFunction->HasSelf())
			{
				return NativeFunction->Bind(Context, *this);
			}
			else
			{
				return FieldValue;
			}
		}
		default:
			VERSE_UNREACHABLE();
			break;
	}
}

inline VValue VObject::LoadField(FAllocationContext Context, const VUniqueString& Name)
{
	const VEmergentType* EmergentType = GetEmergentType();
	return LoadField(Context, *EmergentType->CppClassInfo, EmergentType->Shape->GetField(Name));
}

inline FOpResult VObject::SetField(FAllocationContext Context, const VShape& Shape, const VUniqueString& Name, void* Data, VValue Value)
{
	const VShape::VEntry* Field = Shape.GetField(Name);
	V_DIE_IF(Field == nullptr);
	switch (Field->Type)
	{
		case EFieldType::Offset:
			BitCast<VRestValue*>(Data)[Field->Index].Set(Context, Value);
			return {FOpResult::Return};
		case EFieldType::FProperty:
			return VNativeRef::Set<false>(Context, nullptr, Data, Field->UProperty, Value);
		case EFieldType::FPropertyVar:
			return VNativeRef::Set<false>(Context, nullptr, Data, Field->UProperty, Value.StaticCast<VVar>().Get(Context));
		case EFieldType::FVerseProperty:
			Field->UProperty->ContainerPtrToValuePtr<VRestValue>(Data)->Set(Context, Value);
			return {FOpResult::Return};
		case EFieldType::Constant:
		default:
			VERSE_UNREACHABLE(); // This shouldn't happen since such field's data should be on the shape, not the object.
			break;
	}
}

inline FOpResult VObject::SetField(FAllocationContext Context, const VUniqueString& Name, VValue Value)
{
	const VEmergentType* EmergentType = GetEmergentType();
	return SetField(Context, *EmergentType->Shape, Name, GetData(*EmergentType->CppClassInfo), Value);
}

} // namespace Verse
#endif // WITH_VERSE_VM
