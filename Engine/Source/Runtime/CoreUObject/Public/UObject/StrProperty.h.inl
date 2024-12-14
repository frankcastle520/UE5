// Copyright Epic Games, Inc. All Rights Reserved.

/*******************************************************************************************************************
 * NOTICE                                                                                                          *
 *                                                                                                                 *
 * This file is not intended to be included directly - it will be included by other .h files which have predefined *
 * some macros to be expanded within this file.  As such, it does not have a #pragma once as it is intended to be  *
 * included multiple times with different macro definitions.                                                       *
 *                                                                                                                 *
 * #includes needed to compile this file need to be specified in StrPropertyIncludes.h.inl file rather than here.  *
 *******************************************************************************************************************/
#define UE_INCLUDETOOL_IGNORE_INCONSISTENT_STATE

#ifndef UE_STRPROPERTY_CLASS
	#error "StrProperty.h.inl should only be included after defining UE_STRPROPERTY_CLASS"
#endif
#ifndef UE_STRPROPERTY_STRINGTYPE
	#error "StrProperty.h.inl should only be included after defining UE_STRPROPERTY_STRINGTYPE"
#endif
#ifndef UE_STRPROPERTY_CASTCLASSFLAG
	#error "StrProperty.h.inl should only be included after defining UE_STRPROPERTY_CASTCLASSFLAG"
#endif
#ifndef UE_STRPROPERTY_PROPERTYPARAMSSTRUCT
	#error "StrProperty.h.inl should only be included after defining UE_STRPROPERTY_PROPERTYPARAMSSTRUCT"
#endif

class COREUOBJECT_API UE_STRPROPERTY_CLASS : public TProperty_WithEqualityAndSerializer<UE_STRPROPERTY_STRINGTYPE, FProperty>
{
	DECLARE_FIELD(UE_STRPROPERTY_CLASS, (TProperty_WithEqualityAndSerializer<UE_STRPROPERTY_STRINGTYPE, FProperty>), UE_STRPROPERTY_CASTCLASSFLAG)
public:
	using TTypeFundamentals = Super::TTypeFundamentals;
	using TCppType = TTypeFundamentals::TCppType;

	UE_STRPROPERTY_CLASS(FFieldVariant InOwner, const FName& InName, EObjectFlags InObjectFlags);

	/**
	 * Constructor used for constructing compiled in properties
	 * @param InOwner Owner of the property
	 * @param PropBase Pointer to the compiled in structure describing the property
	 **/
	UE_STRPROPERTY_CLASS(FFieldVariant InOwner, const UECodeGen_Private::UE_STRPROPERTY_PROPERTYPARAMSSTRUCT& Prop);

#if WITH_EDITORONLY_DATA
	explicit UE_STRPROPERTY_CLASS(UField* InField);
#endif // WITH_EDITORONLY_DATA

	// FProperty interface
protected:
	virtual void ExportText_Internal( FString& ValueStr, const void* PropertyValueOrContainer, EPropertyPointerType PropertyPointerType, const void* DefaultValue, UObject* Parent, int32 PortFlags, UObject* ExportRootScope ) const override;
	virtual const TCHAR* ImportText_Internal(const TCHAR* Buffer, void* ContainerOrPropertyPtr, EPropertyPointerType PropertyPointerType, UObject* OwnerObject, int32 PortFlags, FOutputDevice* ErrorText) const override;
public:
	virtual EConvertFromTypeResult ConvertFromType(const FPropertyTag& Tag, FStructuredArchive::FSlot Slot, uint8* Data, UStruct* DefaultsStruct, const uint8* Defaults) override;
	UE_DEPRECATED(5.4, "UnrealHeaderTool only API.  No replacement available.")
	virtual FString GetCPPTypeForwardDeclaration() const override;
	uint32 GetValueTypeHashInternal(const void* Src) const override;
	// End of FProperty interface
};
