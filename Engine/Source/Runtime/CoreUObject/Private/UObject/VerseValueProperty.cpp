// Copyright Epic Games, Inc. All Rights Reserved.

#include "UObject/VerseValueProperty.h"
#include "UObject/GarbageCollectionSchema.h"
#include "UObject/VerseTypes.h"
#include "VerseVM/VVMAbstractVisitor.h"
#include "VerseVM/VVMCell.h"
#include "VerseVM/VVMRestValue.h"
#include "VerseVM/VVMStructuredArchiveVisitor.h"
#include "VerseVM/Inline/VVMValueInline.h"

#if WITH_VERSE_VM || defined(__INTELLISENSE__)

namespace UE::Private
{
	struct FVerseObjectReferenceScan : public Verse::FAbstractVisitor
	{
		UE_NONCOPYABLE(FVerseObjectReferenceScan);

		FVerseObjectReferenceScan(FArchive& InAr)
			: Ar(InAr)
		{
		}

		virtual void VisitNonNull(Verse::VCell*& InCell, const TCHAR* ElementName) override
		{
			AddCell(InCell);
		}

		virtual void VisitNonNull(UObject*& InObject, const TCHAR* ElementName) override
		{
			Ar << InObject;
		}

		template<typename VValueType>
		static void Scan(FArchive& InAr, VValueType& Value)
		{
			FVerseObjectReferenceScan Scanner(InAr);
			Scanner.Visit(Value, TEXT(""));
			while (!Scanner.Stack.IsEmpty())
			{
				Verse::VCell* CurrentCell = Scanner.Stack.Pop();
				CurrentCell->VisitReferences(Scanner);
			}
		}

	private:
		void AddCell(Verse::VCell* InCell)
		{
			bool bIsAlreadyInSet;
			Scanned.Add(InCell, &bIsAlreadyInSet);
			if (!bIsAlreadyInSet)
			{
				Stack.Add(InCell);
			}
		}

		FArchive& Ar;
		TSet<Verse::VCell*> Scanned;
		TArray<Verse::VCell*> Stack;
	};
}

FVValueProperty::FVValueProperty(FFieldVariant InOwner, const FName& InName, EObjectFlags InObjectFlags)
	: Super(InOwner, InName, InObjectFlags)
{
}

FVValueProperty::FVValueProperty(FFieldVariant InOwner, const UECodeGen_Private::FVerseValuePropertyParams& Prop)
	: Super(InOwner, Prop)
{
}

FVRestValueProperty::FVRestValueProperty(FFieldVariant InOwner, const FName& InName, EObjectFlags InObjectFlags)
	: Super(InOwner, InName, InObjectFlags)
{
}

FVRestValueProperty::FVRestValueProperty(FFieldVariant InOwner, const UECodeGen_Private::FVerseValuePropertyParams& Prop)
	: Super(InOwner, Prop)
{
}

template <typename T>
FString TProperty_Verse<T>::GetCPPMacroType(FString& ExtendedTypeText) const
{
	ExtendedTypeText = FString();
	return FString();
}

template <typename T>
bool TProperty_Verse<T>::Identical(const void* A, const void* B, uint32 PortFlags) const
{
	check(A);

	if (nullptr == B) // if the comparand is NULL, we just call this no-match
	{
		return false;
	}

	const TCppType* Lhs = reinterpret_cast<const TCppType*>(A);
	const TCppType* Rhs = reinterpret_cast<const TCppType*>(B);
	return *Lhs == *Rhs;
}

template <typename T>
void TProperty_Verse<T>::SerializeItem(FStructuredArchive::FSlot Slot, void* Value, void const* Defaults) const
{
	TCppType& LocalValue = *reinterpret_cast<TCppType*>(Value);
	FArchive& Ar = Slot.GetUnderlyingArchive();
	if (Ar.IsSaving() || Ar.IsLoading())
	{
		Verse::FStructuredArchiveVisitor::Serialize(Slot, LocalValue);
	}
	else
	{
		UE::Private::FVerseObjectReferenceScan::Scan(Ar, LocalValue); 
		Slot.EnterStream();
	}
}

template <typename T>
void TProperty_Verse<T>::ExportText_Internal(FString& ValueStr, const void* PropertyValueOrContainer, EPropertyPointerType PointerType, const void* DefaultValue, UObject* Parent, int32 PortFlags, UObject* ExportRootScope) const
{
	check(false);
	return;
}

template <typename T>
const TCHAR* TProperty_Verse<T>::ImportText_Internal(const TCHAR* Buffer, void* ContainerOrPropertyPtr, EPropertyPointerType PropertyPointerType, UObject* OwnerObject, int32 PortFlags, FOutputDevice* ErrorText) const
{
	check(false);
	return TEXT("");
}

template <typename T>
bool TProperty_Verse<T>::ContainsObjectReference(TArray<const FStructProperty*>& EncounteredStructProps, EPropertyObjectReferenceType InReferenceType/* = EPropertyObjectReferenceType::Strong*/) const
{
	return true;
}

template <typename T>
void TProperty_Verse<T>::EmitReferenceInfo(UE::GC::FSchemaBuilder& Schema, int32 BaseOffset, TArray<const FStructProperty*>& EncounteredStructProps, UE::GC::FPropertyStack& DebugPath)
{
	for (int32 Idx = 0, Num = FProperty::ArrayDim; Idx < Num; ++Idx)
	{
		Schema.Add(UE::GC::DeclareMember(DebugPath, BaseOffset + FProperty::GetOffset_ForGC() + Idx * sizeof(TCppType), UE::GC::EMemberType::VerseValue));
	}
}

IMPLEMENT_FIELD(FVValueProperty)
IMPLEMENT_FIELD(FVRestValueProperty)

#endif // WITH_VERSE_VM