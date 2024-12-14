// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_VERSE_VM || defined(__INTELLISENSE__)
#include "VerseVM/VVMValueObject.h"
#include "Templates/TypeHash.h"
#include "VerseVM/Inline/VVMAbstractVisitorInline.h"
#include "VerseVM/Inline/VVMArrayBaseInline.h"
#include "VerseVM/Inline/VVMMarkStackVisitorInline.h"
#include "VerseVM/Inline/VVMObjectInline.h"
#include "VerseVM/Inline/VVMShapeInline.h"
#include "VerseVM/Inline/VVMValueObjectInline.h"
#include "VerseVM/VVMCppClassInfo.h"

namespace Verse
{
DEFINE_DERIVED_VCPPCLASSINFO(VValueObject);

template <typename TVisitor>
void VValueObject::VisitReferencesImpl(TVisitor& Visitor)
{
	const VEmergentType* EmergentType = GetEmergentType();
	if constexpr (TVisitor::bIsAbstractVisitor)
	{
		Visitor.VisitObject(TEXT(""), EmergentType->Type->StaticCast<VClass>().GetName(), [this, &Visitor, EmergentType] {
			const VCppClassInfo* CppClassInfo = EmergentType->CppClassInfo;
			for (VShape::FieldsMap::TConstIterator I = EmergentType->Shape->Fields; I; ++I)
			{
				FString Key = I->Key->AsString();
				switch (I->Value.Type)
				{
					case EFieldType::Offset:
					{
						VRestValue& Value = GetFieldData(*CppClassInfo)[I->Value.Index];
						::Verse::Visit(Visitor, Value, *Key);
						break;
					}
					case EFieldType::FProperty:
					{
						check(I->Value.UProperty->IsA<FVRestValueProperty>());
						VRestValue& Value = *I->Value.UProperty->ContainerPtrToValuePtr<VRestValue>(GetData(*CppClassInfo));
						::Verse::Visit(Visitor, Value, *Key);
						break;
					}
					case EFieldType::Constant:
					{
						VValue Value = I->Value.Value.Get();
						::Verse::Visit(Visitor, Value, *Key);
						break;
					}
				}
			}
		});
	}
	else
	{
		VRestValue* Data = GetFieldData(*EmergentType->CppClassInfo);
		uint64 NumIndexedFields = EmergentType->Shape->NumIndexedFields;
		Visitor.Visit(Data, Data + NumIndexedFields);
	}
}

bool VValueObject::EqualImpl(FAllocationContext Context, VCell* Other, const TFunction<void(VValue, VValue)>& HandlePlaceholder)
{
	if (!IsStruct())
	{
		return this == Other;
	}

	if (!Other->IsA<VObject>())
	{
		return false;
	}

	const VEmergentType* EmergentType = GetEmergentType();
	const VEmergentType* OtherEmergentType = Other->GetEmergentType();

	if (EmergentType->Type != OtherEmergentType->Type)
	{
		return false;
	}

	if (EmergentType->Shape->Fields.Num() != OtherEmergentType->Shape->Fields.Num())
	{
		return false;
	}

	// TODO: Optimize for when objects share emergent type
	VObject& OtherObject = Other->StaticCast<VObject>();
	for (VShape::FieldsMap::TConstIterator It = EmergentType->Shape->Fields; It; ++It)
	{
		VValue FieldValue = LoadField(Context, *EmergentType->CppClassInfo, &It->Value);
		if (!FieldValue)
		{
			return false;
		}

		if (!VValue::Equal(Context, OtherObject.LoadField(Context, *It.Key().Get()), FieldValue, HandlePlaceholder))
		{
			return false;
		}
	}
	return true;
}

// TODO: Make this (And all other container TypeHash funcs) handle placeholders appropriately
uint32 VValueObject::GetTypeHashImpl()
{
	if (!IsStruct())
	{
		return PointerHash(this);
	}

	const VEmergentType* EmergentType = GetEmergentType();
	VRestValue* Data = GetFieldData(*EmergentType->CppClassInfo);

	// Hash nominal type
	uint32 Result = PointerHash(EmergentType->Type.Get());
	for (VShape::FieldsMap::TConstIterator It = EmergentType->Shape->Fields; It; ++It)
	{
		// Hash Field Name
		Result = ::HashCombineFast(Result, GetTypeHash(It.Key()));

		// Hash Value
		if (It.Value().Type == EFieldType::Constant)
		{
			Result = ::HashCombineFast(Result, GetTypeHash(It.Value().Value));
		}
		else
		{
			Result = ::HashCombineFast(Result, GetTypeHash(Data[It.Value().Index]));
		}
	}
	return Result;
}

VValue VValueObject::MeltImpl(FAllocationContext Context)
{
	V_DIE_UNLESS(IsStruct());

	VEmergentType& EmergentType = *GetEmergentType();
	VEmergentType& NewEmergentType = EmergentType.GetOrCreateMeltTransition(Context);

	VValueObject& NewObject = NewUninitialized(Context, NewEmergentType);
	NewObject.SetIsStruct();
	if (&EmergentType == &NewEmergentType)
	{
		VRestValue* Data = GetFieldData(*EmergentType.CppClassInfo);
		VRestValue* TargetData = NewObject.GetFieldData(*EmergentType.CppClassInfo);
		uint64 NumIndexedFields = EmergentType.Shape->NumIndexedFields;
		for (uint64 I = 0; I < NumIndexedFields; ++I)
		{
			VValue MeltResult = VValue::Melt(Context, Data[I].Get(Context));
			if (MeltResult.IsPlaceholder())
			{
				return MeltResult;
			}

			TargetData[I].Set(Context, MeltResult);
		}
	}
	else
	{
		for (auto It = EmergentType.Shape->CreateFieldsIterator(); It; ++It)
		{
			VValue MeltResult = VValue::Melt(Context, LoadField(Context, *EmergentType.CppClassInfo, &It->Value));
			if (MeltResult.IsPlaceholder())
			{
				return MeltResult;
			}
			FOpResult Result = NewObject.SetField(Context, *It->Key.Get(), MeltResult);
			V_DIE_UNLESS(Result.Kind == FOpResult::Return);
		}
	}

	return VValue(NewObject);
}

VValue VValueObject::FreezeImpl(FAllocationContext Context)
{
	V_DIE_UNLESS(IsStruct());

	VEmergentType& EmergentType = *GetEmergentType();
	VValueObject& NewObject = NewUninitialized(Context, EmergentType);
	NewObject.SetIsStruct();

	// Mutable structs have all fields as indexed fields in the object.
	uint64 NumIndexedFields = EmergentType.Shape->NumIndexedFields;
	V_DIE_UNLESS(NumIndexedFields == EmergentType.Shape->GetNumFields());

	VRestValue* Data = GetFieldData(*EmergentType.CppClassInfo);
	VRestValue* TargetData = NewObject.GetFieldData(*EmergentType.CppClassInfo);
	for (uint64 I = 0; I < NumIndexedFields; ++I)
	{
		TargetData[I].Set(Context, VValue::Freeze(Context, Data[I].Get(Context)));
	}
	return VValue(NewObject);
}

} // namespace Verse
#endif // WITH_VERSE_VM || defined(__INTELLISENSE__)
