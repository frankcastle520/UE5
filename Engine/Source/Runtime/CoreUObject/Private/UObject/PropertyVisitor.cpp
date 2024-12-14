// Copyright Epic Games, Inc. All Rights Reserved.

#include "UObject/PropertyVisitor.h"
#include "Containers/UnrealString.h"
#include "UObject/NameTypes.h"
#include "UObject/UnrealType.h"
#include "Serialization/ArchiveSerializedPropertyChain.h"

//----------------------------------------------------------------------//
// FPropertyVisitorInfo 
//----------------------------------------------------------------------//

bool FPropertyVisitorInfo::Identical(const FPropertyVisitorInfo& Other) const
{
	return Property == Other.Property
		&& ParentStructType == Other.ParentStructType
		&& Index == Other.Index
		&& PropertyInfo == Other.PropertyInfo
		&& bContainsInnerProperties == Other.bContainsInnerProperties;
}

//----------------------------------------------------------------------//
// FPropertyVisitorPath 
//----------------------------------------------------------------------//

FPropertyVisitorPath::Iterator FPropertyVisitorPath::InvalidIterator()
{
	static FPropertyVisitorPath EmptyPath;
	return Iterator(EmptyPath.GetPath());
}

FPropertyVisitorPath::FPropertyVisitorPath(const FPropertyChangedEvent& PropertyEvent, const FEditPropertyChain& PropertyChain)
{
	const FEditPropertyChain::TDoubleLinkedListNode* PropertyIterator = PropertyChain.GetActiveMemberNode() ? PropertyChain.GetActiveMemberNode() : PropertyChain.GetHead();
	while(PropertyIterator)
	{
		const FProperty* CurrentProperty = PropertyIterator->GetValue();
		int32 ArrayIndex = PropertyEvent.GetArrayIndex(CurrentProperty->GetName());
		if (ArrayIndex == INDEX_NONE)
		{
			Push(FPropertyVisitorInfo{CurrentProperty});
		}
		else if (CurrentProperty->ArrayDim > 1)
		{
			Push(FPropertyVisitorInfo{CurrentProperty, ArrayIndex, EPropertyVisitorInfoType::StaticArrayIndex});
		}
		else
		{
			// Only container is left, no easy way yet to know if we are editing a Key/value of a map
			Push(FPropertyVisitorInfo{CurrentProperty, ArrayIndex, EPropertyVisitorInfoType::ContainerIndex});
		}
		PropertyIterator = PropertyIterator->GetNextNode();
	}
}

FPropertyVisitorPath::FPropertyVisitorPath(const FArchiveSerializedPropertyChain& PropertyChain)
{
	TArray<class FProperty*, TInlineAllocator<8>>::TConstIterator PropertyIterator = PropertyChain.GetRootIterator();
	while (PropertyIterator)
	{
		const FProperty* CurrentProperty = (*PropertyIterator);

		// @todo add container index eventually if we ever have this information
		Push(FPropertyVisitorInfo{CurrentProperty});

		++PropertyIterator;
	}
}

FString FPropertyVisitorPath::ToString(const TCHAR* Separator /*= TEXT(".")*/) const
{
	return PropertyVisitorHelpers::PathToString(Path, Separator);
}

void FPropertyVisitorPath::ToString(FStringBuilderBase& Out, const TCHAR* Separator /*= TEXT(".")*/) const
{
	PropertyVisitorHelpers::PathToString(Path, Out, Separator);
}

void FPropertyVisitorPath::AppendString(FStringBuilderBase& Out, const TCHAR* Separator /*= TEXT(".")*/) const
{
	PropertyVisitorHelpers::PathAppendString(Path, Out, Separator);
}

bool FPropertyVisitorPath::Contained(const FPropertyVisitorPath& Other, bool* bIsEqual) const
{
	return PropertyVisitorHelpers::PathIsContainedWithin(Path, Other.Path, bIsEqual);
}

void* FPropertyVisitorPath::GetPropertyDataPtr(UObject* Object) const
{
	checkf(Object, TEXT("Expecting an valid object"));
	return PropertyVisitorHelpers::ResolveVisitedPath(Object->GetClass(), Object, *this);
}

FArchiveSerializedPropertyChain FPropertyVisitorPath::ToSerializedPropertyChain() const
{
	return PropertyVisitorHelpers::PathToSerializedPropertyChain(Path);
}

FString PropertyVisitorHelpers::PathToString(TArrayView<const FPropertyVisitorInfo> Path, const TCHAR* Separator)
{
	TStringBuilder<FName::StringBufferSize> PropertyPath;
	PathAppendString(Path, PropertyPath, Separator);
	return PropertyPath.ToString();
}

void PropertyVisitorHelpers::PathToString(TArrayView<const FPropertyVisitorInfo> Path, FStringBuilderBase& Out, const TCHAR* Separator)
{
	Out.Reset();
	PathAppendString(Path, Out, Separator);
}

void PropertyVisitorHelpers::PathAppendString(TArrayView<const FPropertyVisitorInfo> Path, FStringBuilderBase& Out, const TCHAR* Separator)
{
	bool bFirstEntry = true;
	for (const FPropertyVisitorInfo& Entry : Path)
	{
		bool bDisplayPropertyName = false;
		bool bDisplayIndex = false;
		FString Suffix(TEXT(""));
		switch (Entry.PropertyInfo)
		{
			case EPropertyVisitorInfoType::None:
				bDisplayPropertyName = true;
				break;
			case EPropertyVisitorInfoType::StaticArrayIndex:
				bDisplayPropertyName = true;
				bDisplayIndex = true;
				break;
			case EPropertyVisitorInfoType::ContainerIndex:
				bDisplayIndex = true;
				break;
			case EPropertyVisitorInfoType::MapKey:
				bDisplayIndex = true;
				Suffix = TEXT("Key");
				break;
			case EPropertyVisitorInfoType::MapValue:
				bDisplayIndex = true;
				Suffix = TEXT("Value");
				break;
			default:
				checkf(false, TEXT("Unsupported enum value"));
				break;
		}
		if (bDisplayPropertyName)
		{
			if (bFirstEntry)
			{
				bFirstEntry = false;
			}
			else
			{
				Out.Append(Separator);
			}
			Out.Append(Entry.Property->GetAuthoredName());
		}
		if (bDisplayIndex)
		{
			checkf(Entry.Index != INDEX_NONE, TEXT("Expecting the index to be valid"));
			Out.Appendf(TEXT("[%d]"), Entry.Index);
		}
		if (Suffix.Len())
		{
			Out.Append(Separator);
			Out.Append(Suffix);
		}
	}
}

bool PropertyVisitorHelpers::PathIsContainedWithin(TArrayView<const FPropertyVisitorInfo> Path, TArrayView<const FPropertyVisitorInfo> OtherPath, bool* bIsEqual)
{
	int32 i;
	for (i = 0; i < Path.Num(); ++i)
	{
		if (i >= OtherPath.Num())
		{
			// The other path is smaller than this one, so not contained in
			break;
		}

		const FPropertyVisitorInfo& PathInfo = Path[i];
		const FPropertyVisitorInfo& OtherPathInfo = OtherPath[i];

		if (PathInfo.Property != OtherPathInfo.Property)
		{
			// The property is different, so not contained in
			break;
		}

		if (PathInfo.PropertyInfo != OtherPathInfo.PropertyInfo)
		{
			if (PathInfo.PropertyInfo != EPropertyVisitorInfoType::None)
			{
				// The property info type is different and it is not none, so not contained in
				break;
			}
		}
		else if (PathInfo.Index != OtherPathInfo.Index)
		{
			// The index is different, so not contained in
			break;
		}
	}
	if (bIsEqual)
	{
		*bIsEqual = i == OtherPath.Num();
	}
	return i == Path.Num();
}

FArchiveSerializedPropertyChain PropertyVisitorHelpers::PathToSerializedPropertyChain(TArrayView<const FPropertyVisitorInfo> Path)
{
	FArchiveSerializedPropertyChain Chain;
	for (const FPropertyVisitorInfo& Info : Path)
	{
		// @todo add container index eventually if we ever have this information
		Chain.PushProperty(const_cast<FProperty*>(Info.Property), Info.Property->IsEditorOnlyProperty());
	}
	return Chain;
}
