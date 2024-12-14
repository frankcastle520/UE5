// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_VERSE_VM || defined(__INTELLISENSE__)
#include "HAL/Platform.h"
#include "UObject/UnrealType.h"
#include "UObject/VerseTypes.h"

// Specialization of the property type fundamentals for our two types
template<>
FORCEINLINE TCHAR const* TPropertyTypeFundamentals<Verse::TWriteBarrier<Verse::VValue>>::GetTypeName()
{
	return TEXT("Verse::TWriteBarrier<Verse::VValue>");
}

template<>
FORCEINLINE TCHAR const* TPropertyTypeFundamentals<Verse::VRestValue>::GetTypeName()
{
	return TEXT("Verse::VRestValue");
}

template<>
FORCEINLINE Verse::VRestValue TPropertyTypeFundamentals<Verse::VRestValue>::GetDefaultPropertyValue()
{
	return Verse::VRestValue(0);
}

template<>
FORCEINLINE Verse::VRestValue* TPropertyTypeFundamentals<Verse::VRestValue>::InitializePropertyValue(void* A)
{
	return new (A) Verse::VRestValue(0);
}

// Template base class for the verse property types
template <typename InTCppType>
class TProperty_Verse : public TProperty<InTCppType, FProperty>
{
public:
	using Super = TProperty<InTCppType, FProperty>;
	using TCppType = InTCppType;

	TProperty_Verse(EInternal InInternal, FFieldClass* InClass)
		: Super(EC_InternalUseOnlyConstructor, InClass)
	{
	}

	TProperty_Verse(FFieldVariant InOwner, const FName& InName, EObjectFlags InObjectFlags)
		: Super(InOwner, InName, InObjectFlags)
	{
	}

	TProperty_Verse(FFieldVariant InOwner, const UECodeGen_Private::FVerseValuePropertyParams& Prop)
		: Super(InOwner, reinterpret_cast<const UECodeGen_Private::FPropertyParamsBaseWithOffset&>(Prop))
	{
	}

#if WITH_EDITORONLY_DATA
	explicit TProperty_Verse(UField* InField)
		: Super(InField)
	{
	}
#endif // WITH_EDITORONLY_DATA

	// UHT interface
	COREUOBJECT_API virtual FString GetCPPMacroType(FString& ExtendedTypeText) const override;
	// End of UHT interface

	// FProperty interface
	COREUOBJECT_API virtual bool Identical(const void* A, const void* B, uint32 PortFlags) const override;
	COREUOBJECT_API virtual void SerializeItem(FStructuredArchive::FSlot Slot, void* Value, void const* Defaults) const override;
	COREUOBJECT_API virtual void ExportText_Internal(FString& ValueStr, const void* PropertyValueOrContainer, EPropertyPointerType PointerType, const void* DefaultValue, UObject* Parent, int32 PortFlags, UObject* ExportRootScope) const override;
	COREUOBJECT_API virtual const TCHAR* ImportText_Internal(const TCHAR* Buffer, void* ContainerOrPropertyPtr, EPropertyPointerType PropertyPointerType, UObject* OwnerObject, int32 PortFlags, FOutputDevice* ErrorText) const override;
	COREUOBJECT_API virtual bool ContainsObjectReference(TArray<const FStructProperty*>& EncounteredStructProps, EPropertyObjectReferenceType InReferenceType = EPropertyObjectReferenceType::Strong) const override;
	COREUOBJECT_API virtual void EmitReferenceInfo(UE::GC::FSchemaBuilder& Schema, int32 BaseOffset, TArray<const FStructProperty*>& EncounteredStructProps, UE::GC::FPropertyStack& DebugPath) override;

	COREUOBJECT_API virtual bool HasIntrusiveUnsetOptionalState() const override
	{
		return false;
	}
	// End of FProperty interface
};

//
// Metadata for a property of FVValueProperty type.
//
class FVValueProperty : public TProperty_Verse<Verse::TWriteBarrier<Verse::VValue>>
{
	DECLARE_FIELD_API(FVValueProperty, TProperty_Verse<Verse::TWriteBarrier<Verse::VValue>>, CASTCLASS_FVValueProperty, COREUOBJECT_API)

public:
	COREUOBJECT_API FVValueProperty(FFieldVariant InOwner, const FName& InName, EObjectFlags InObjectFlags);

	/**
	 * Constructor used for constructing compiled in properties
	 * @param InOwner Owner of the property
	 * @param Prop Pointer to the compiled in structure describing the property
	 **/
	COREUOBJECT_API FVValueProperty(FFieldVariant InOwner, const UECodeGen_Private::FVerseValuePropertyParams& Prop);
};

//
// Metadata for a property of FVRestValueProperty type.
//
class FVRestValueProperty : public TProperty_Verse<Verse::VRestValue>
{
	DECLARE_FIELD_API(FVRestValueProperty, TProperty_Verse<Verse::VRestValue>, CASTCLASS_FVRestValueProperty, COREUOBJECT_API)

public:
	COREUOBJECT_API FVRestValueProperty(FFieldVariant InOwner, const FName& InName, EObjectFlags InObjectFlags);

	/**
	 * Constructor used for constructing compiled in properties
	 * @param InOwner Owner of the property
	 * @param Prop Pointer to the compiled in structure describing the property
	 **/
	COREUOBJECT_API FVRestValueProperty(FFieldVariant InOwner, const UECodeGen_Private::FVerseValuePropertyParams& Prop);
};

#endif // WITH_VERSE_VM
