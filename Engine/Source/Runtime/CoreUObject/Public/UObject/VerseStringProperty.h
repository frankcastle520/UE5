// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/UnrealType.h"
#include "VerseVM/VVMNativeString.h"

// HACK
constexpr const EClassCastFlags CASTCLASS_FVerseStringProperty = EClassCastFlags(1ULL << 59);

Expose_TNameOf(Verse::FNativeString)

typedef TProperty_WithEqualityAndSerializer<Verse::FNativeString, FProperty> FVerseStringProperty_Super;

// FVerseStringProperty ?
class COREUOBJECT_API FVerseStringProperty : public FVerseStringProperty_Super
{
	DECLARE_FIELD(FVerseStringProperty, FVerseStringProperty_Super, CASTCLASS_FVerseStringProperty)
public:
	typedef FVerseStringProperty_Super::TTypeFundamentals TTypeFundamentals;
	typedef TTypeFundamentals::TCppType TCppType;

	FProperty* Inner;

	FVerseStringProperty(FFieldVariant InOwner, const FName& InName, EObjectFlags InObjectFlags)
		: FVerseStringProperty_Super(InOwner, InName, InObjectFlags)
		, Inner(nullptr)
	{
	}

	virtual ~FVerseStringProperty();
	
	// FField interface
	virtual void Serialize(FArchive& Ar) override;
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	// End of FField interface

	// FProperty interface
protected:
	UE_DEPRECATED(5.4, "UnrealHeaderTool only API.  No replacement available.")
	virtual FString GetCPPTypeForwardDeclaration() const override { return TEXT("namespace Verse { class FNativeString; } namespace verse { using string = Verse::FNativeString; }"); }
	virtual void ExportText_Internal(FString& ValueStr, const void* PropertyValueOrContainer, EPropertyPointerType PropertyPointerType, const void* DefaultValue, UObject* Parent, int32 PortFlags, UObject* ExportRootScope) const override;
	virtual const TCHAR* ImportText_Internal(const TCHAR* Buffer, void* ContainerOrPropertyPtr, EPropertyPointerType PropertyPointerType, UObject* OwnerObject, int32 PortFlags, FOutputDevice* ErrorText) const override;
	uint32 GetValueTypeHashInternal(const void* Src) const override;
	virtual bool LoadTypeName(UE::FPropertyTypeName Type, const FPropertyTag* Tag = nullptr) override;
	virtual void SaveTypeName(UE::FPropertyTypeNameBuilder& Type) const override;
	// End of FProperty interface
};
