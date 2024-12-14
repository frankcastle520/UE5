// Copyright Epic Games, Inc. All Rights Reserved.

#include "UObject/VerseStringProperty.h"

#include "UObject/PropertyTypeName.h"

IMPLEMENT_FIELD(FVerseStringProperty)

FVerseStringProperty::~FVerseStringProperty()
{
	delete Inner;
	Inner = nullptr;
}

uint32 FVerseStringProperty::GetValueTypeHashInternal(const void* Src) const
{
	return GetTypeHash(*(const Verse::FNativeString*)Src);
}

void FVerseStringProperty::ExportText_Internal(FString& ValueStr, const void* PropertyValueOrContainer, EPropertyPointerType PropertyPointerType, const void* DefaultValue, UObject* Parent, int32 PortFlags, UObject* ExportRootScope) const
{
	Verse::FNativeString VerseString;
	if (PropertyPointerType == EPropertyPointerType::Container && HasGetter())
	{
		GetValue_InContainer(PropertyValueOrContainer, &VerseString);
	}
	else
	{
		VerseString = *(Verse::FNativeString*)PointerToValuePtr(PropertyValueOrContainer, PropertyPointerType);
	}

	FString StringValue = FString(VerseString);

	if (!(PortFlags & PPF_Delimited))
	{
		ValueStr += StringValue;
	}
	else if (StringValue.Len() > 0)
	{
		ValueStr += FString::Printf(TEXT("\"%s\""), *(StringValue.ReplaceCharWithEscapedChar()));
	}
	else
	{
		ValueStr += TEXT("\"\"");
	}
}

const TCHAR* FVerseStringProperty::ImportText_Internal(const TCHAR* Buffer, void* ContainerOrPropertyPtr, EPropertyPointerType PropertyPointerType, UObject* Parent, int32 PortFlags, FOutputDevice* ErrorText) const
{
	FString ImportedText;
	if (!(PortFlags & PPF_Delimited))
	{
		ImportedText = Buffer;

		// in order to indicate that the value was successfully imported, advance the buffer past the last character that was imported
		Buffer += FCString::Strlen(Buffer);
	}
	else
	{
		// require quoted string here
		if (*Buffer != TCHAR('"'))
		{
			ErrorText->Logf(TEXT("Missing opening '\"' in string property value: %s"), Buffer);
			return NULL;
		}
		const TCHAR* Start = Buffer;
		FString Temp;
		Buffer = FPropertyHelpers::ReadToken(Buffer, Temp);
		if (Buffer == NULL)
		{
			return NULL;
		}
		if (Buffer > Start && Buffer[-1] != TCHAR('"'))
		{
			ErrorText->Logf(TEXT("Missing terminating '\"' in string property value: %s"), Start);
			return NULL;
		}
		ImportedText = MoveTemp(Temp);
	}
	if (PropertyPointerType == EPropertyPointerType::Container && HasSetter())
	{
		SetValue_InContainer(ContainerOrPropertyPtr, Verse::FNativeString(ImportedText));
	}
	else
	{
		*(Verse::FNativeString*)PointerToValuePtr(ContainerOrPropertyPtr, PropertyPointerType) = Verse::FNativeString(MoveTemp(ImportedText));
	}
	return Buffer;
}

void FVerseStringProperty::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);
	
	SerializeSingleField(Ar, Inner, this);
	checkSlow(Inner);
}

void FVerseStringProperty::AddReferencedObjects(FReferenceCollector& Collector)
{
	Super::AddReferencedObjects(Collector);
	if (Inner)
	{
		Inner->AddReferencedObjects(Collector);
	}
}

bool FVerseStringProperty::LoadTypeName(UE::FPropertyTypeName Type, const FPropertyTag* Tag)
{
	if (!Super::LoadTypeName(Type, Tag))
	{
		return false;
	}

	Inner = new FByteProperty(this, GetFName(), RF_NoFlags);
	return true;
}

void FVerseStringProperty::SaveTypeName(UE::FPropertyTypeNameBuilder& Type) const
{
	Super::SaveTypeName(Type);
	check(!Inner || Inner->IsA<FByteProperty>());
}
