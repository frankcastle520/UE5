// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/DataflowNode.h"

#include "ChaosLog.h"
#include "Dataflow/DataflowInputOutput.h"
#include "Dataflow/DataflowArchive.h"
#include "Serialization/ObjectWriter.h"
#include "Serialization/ObjectReader.h"
#include "Templates/TypeHash.h"
#include "Dataflow/DataflowNodeFactory.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DataflowNode)

const FName FDataflowNode::DataflowInput = TEXT("DataflowInput");
const FName FDataflowNode::DataflowOutput = TEXT("DataflowOutput");
const FName FDataflowNode::DataflowPassthrough = TEXT("DataflowPassthrough");
const FName FDataflowNode::DataflowIntrinsic = TEXT("DataflowIntrinsic");

const FLinearColor FDataflowNode::DefaultNodeTitleColor = FLinearColor(1.f, 1.f, 0.8f);
const FLinearColor FDataflowNode::DefaultNodeBodyTintColor = FLinearColor(0.f, 0.f, 0.f, 0.5f);

const FName FDataflowAnyType::TypeName = TEXT("FDataflowAnyType");

namespace UE::Dataflow::Private
{
static uint32 GetArrayElementOffsetFromReference(const FArrayProperty* const ArrayProperty, const UE::Dataflow::FConnectionReference& Reference)
{
	check(ArrayProperty);
	if (const void* const AddressAtIndex = ArrayProperty->GetValueAddressAtIndex_Direct(ArrayProperty->Inner, const_cast<void*>(Reference.ContainerReference), Reference.Index))
	{
		check((size_t)Reference.Reference >= (size_t)AddressAtIndex);
		check((int32)((size_t)Reference.Reference - (size_t)AddressAtIndex) < ArrayProperty->Inner->GetElementSize());
		return (uint32)((size_t)Reference.Reference - (size_t)AddressAtIndex);
	}
	return INDEX_NONE;
}

static const FProperty* FindProperty(const UStruct* Struct, const void* StructValue, const void* InProperty, const FName& PropertyName, TArray<const FProperty*>* OutPropertyChain)
{
	const FProperty* Property = nullptr;
	for (FPropertyValueIterator PropertyIt(FProperty::StaticClass(), Struct, StructValue); PropertyIt; ++PropertyIt)
	{
		if (InProperty == PropertyIt.Value() && (PropertyName == NAME_None || PropertyName == PropertyIt.Key()->GetName()))
		{
			Property = PropertyIt.Key();
			if (OutPropertyChain)
			{
				PropertyIt.GetPropertyChain(*OutPropertyChain);
			}
			break;
		}
	}
	return Property;
}

static const FProperty& FindPropertyChecked(const UStruct* Struct, const void* StructValue, const void* InProperty, const FName& PropertyName, TArray<const FProperty*>* OutPropertyChain)
{
	const FProperty* Property = FindProperty(Struct, StructValue, InProperty, PropertyName, OutPropertyChain);
	check(Property);
	return *Property;
}

static FString GetPinToolTipFromProperty(const FProperty* Property)
{
#if WITH_EDITORONLY_DATA
	check(Property);
	if (Property->HasMetaData(TEXT("Tooltip")))
	{
		const FString ToolTipStr = Property->GetToolTipText(true).ToString();
		if (ToolTipStr.Len() > 0)
		{
			TArray<FString> OutArr;
			const int32 NumElems = ToolTipStr.ParseIntoArray(OutArr, TEXT(":\r\n"));

			if (NumElems == 2)
			{
				return OutArr[1];  // Return tooltip meta text
			}
			else if (NumElems == 1)
			{
				return OutArr[0];  // Return doc comment
			}
		}
	}
#endif
	return "";
}

static TArray<FString> GetPinMetaDataFromProperty(const FProperty* Property)
{
	TArray<FString> MetaDataStrArr;
#if WITH_EDITORONLY_DATA
	check(Property);
	if (Property->HasMetaData(FDataflowNode::DataflowPassthrough))
	{
		MetaDataStrArr.Add("Passthrough");
	}
	if (Property->HasMetaData(FDataflowNode::DataflowIntrinsic))
	{
		MetaDataStrArr.Add("Intrinsic");
	}
#endif
	return MetaDataStrArr;
}
};

//
// Inputs
//

bool FDataflowNode::OutputSupportsType(FName InName, FName InType) const
{
	if (const FDataflowOutput* Output = FindOutput(InName))
	{
		return Output->SupportsType(InType);
	}
	return false;
}

bool FDataflowNode::InputSupportsType(FName InName, FName InType) const
{
	if (const FDataflowInput* Input = FindInput(InName))
	{
		return Input->SupportsType(InType);
	}
	return false;
}

void FDataflowNode::AddInput(FDataflowInput* InPtr)
{
	if (InPtr)
	{
		for (const TPair<UE::Dataflow::FConnectionKey, FDataflowInput*>& Elem : ExpandedInputs)
		{
			const FDataflowInput* const In = Elem.Value;
			ensureMsgf(!In->GetName().IsEqual(InPtr->GetName()), TEXT("Add Input Failed: Existing Node input already defined with name (%s)"), *InPtr->GetName().ToString());
		}

		check(InPtr->GetOwningNode() == this);

		const UE::Dataflow::FConnectionKey Key(InPtr->GetOffset(), InPtr->GetContainerIndex(), InPtr->GetContainerElementOffset());
		if (ensure(!ExpandedInputs.Contains(Key)))
		{
			ExpandedInputs.Add(Key, InPtr);
		}
	}
}

int32 FDataflowNode::GetNumInputs() const
{
	return ExpandedInputs.Num();
}

FDataflowInput* FDataflowNode::FindInput(FName InName)
{
	for (TPair<UE::Dataflow::FConnectionKey, FDataflowInput*>& Elem : ExpandedInputs)
	{
		FDataflowInput* const Con = Elem.Value;
		if (Con->GetName().IsEqual(InName))
		{
			return Con;
		}
	}
	return nullptr;
}

const FDataflowInput* FDataflowNode::FindInput(FName InName) const
{
	for (const TPair<UE::Dataflow::FConnectionKey, FDataflowInput*>& Elem : ExpandedInputs)
	{
		const FDataflowInput* const Con = Elem.Value;
		if (Con->GetName().IsEqual(InName))
		{
			return Con;
		}
	}
	return nullptr;
}

const FDataflowInput* FDataflowNode::FindInput(const UE::Dataflow::FConnectionKey& Key) const
{
	if (const FDataflowInput* const* Con = ExpandedInputs.Find(Key))
	{
		check(*Con);
		return *Con;
	}
	return nullptr;
}

const FDataflowInput* FDataflowNode::FindInput(const UE::Dataflow::FConnectionReference& Reference) const
{
	const UE::Dataflow::FConnectionKey Key = GetKeyFromReference(Reference);
	if (const FDataflowInput* const Con = FindInput(Key))
	{
		check(Con->RealAddress() == Reference.Reference);
		return Con;
	}
	if (Reference.ContainerReference == nullptr && !InputArrayProperties.IsEmpty())
	{
		// Search through all connections to see if Reference is the RealAddress of an array property.
		for (const TPair<UE::Dataflow::FConnectionKey, FDataflowInput*>& Elem : ExpandedInputs)
		{
			const FDataflowInput* const Con = Elem.Value;
			if (Con->RealAddress() == Reference.Reference)
			{
				return Con;
			}
		}
	}
	return nullptr;
}

FDataflowInput* FDataflowNode::FindInput(const UE::Dataflow::FConnectionKey& Key)
{
	if (FDataflowInput* const* Con = ExpandedInputs.Find(Key))
	{
		check(*Con);
		return *Con;
	}
	return nullptr;
}

FDataflowInput* FDataflowNode::FindInput(const UE::Dataflow::FConnectionReference& Reference)
{
	const UE::Dataflow::FConnectionKey Key = GetKeyFromReference(Reference);
	if (FDataflowInput* const Con = FindInput(Key))
	{
		check(Con->RealAddress() == Reference.Reference);
		return Con;
	}
	if (Reference.ContainerReference == nullptr && !InputArrayProperties.IsEmpty())
	{
		// Search through all connections to see if Reference is the RealAddress of an array property.
		for (TPair<UE::Dataflow::FConnectionKey, FDataflowInput*>& Elem : ExpandedInputs)
		{
			FDataflowInput* const Con = Elem.Value;
			if (Con->RealAddress() == Reference.Reference)
			{
				return Con;
			}
		}
	}
	return nullptr;
}

const FDataflowInput* FDataflowNode::FindInput(const FGuid& InGuid) const
{
	for (const TPair<UE::Dataflow::FConnectionKey, FDataflowInput*>& Elem : ExpandedInputs)
	{
		const FDataflowInput* const Con = Elem.Value;
		if (Con->GetGuid() == InGuid)
		{
			return Con;
		}
	}
	return nullptr;
}

TArray< FDataflowInput* > FDataflowNode::GetInputs() const
{
	TArray< FDataflowInput* > Result;
	ExpandedInputs.GenerateValueArray(Result);
	return Result;
}

void FDataflowNode::ClearInputs()
{
	for (TPair<UE::Dataflow::FConnectionKey, FDataflowInput*>& Elem : ExpandedInputs)
	{
		FDataflowInput* const Con = Elem.Value;
		delete Con;
	}
	ExpandedInputs.Reset();
}

bool FDataflowNode::HasHideableInputs() const
{
	for (const TPair<UE::Dataflow::FConnectionKey, FDataflowInput*>& Elem : ExpandedInputs)
	{
		const FDataflowInput* const Con = Elem.Value;
		if (Con->GetCanHidePin())
		{
			return true;
		}
	}
	return false;
}

bool FDataflowNode::HasHiddenInputs() const
{
	for (const TPair<UE::Dataflow::FConnectionKey, FDataflowInput*>& Elem : ExpandedInputs)
	{
		const FDataflowInput* const Con = Elem.Value;
		if (Con->GetPinIsHidden())
		{
			return true;
		}
	}
	return false;
}

//
// Outputs
//


void FDataflowNode::AddOutput(FDataflowOutput* InPtr)
{
	if (InPtr)
	{
PRAGMA_DISABLE_DEPRECATION_WARNINGS  // Until Outputs becomes private
		for (const TPair<int32, FDataflowOutput*>& Elem : Outputs)
PRAGMA_ENABLE_DEPRECATION_WARNINGS
		{
			const FDataflowOutput* const Out = Elem.Value;
			ensureMsgf(!Out->GetName().IsEqual(InPtr->GetName()), TEXT("Add Output Failed: Existing Node output already defined with name (%s)"), *InPtr->GetName().ToString());
		}

		check(InPtr->GetOwningNode() == this);

		const uint32 PropertyOffset = InPtr->GetOffset();
PRAGMA_DISABLE_DEPRECATION_WARNINGS  // Until Outputs becomes private
		if (ensure(!Outputs.Contains(PropertyOffset)))
		{
			Outputs.Add(PropertyOffset, InPtr);
		}
PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}
}

FDataflowOutput* FDataflowNode::FindOutput(uint32 InGuidHash)
{
PRAGMA_DISABLE_DEPRECATION_WARNINGS  // Until Outputs becomes private
	for (TPair<int32, FDataflowOutput*>& Elem : Outputs)
PRAGMA_ENABLE_DEPRECATION_WARNINGS
	{
		FDataflowOutput* const Con = Elem.Value;
		if (GetTypeHash(Con->GetGuid()) == InGuidHash)
		{
			return Con;
		}
	}
	return nullptr;
}


FDataflowOutput* FDataflowNode::FindOutput(FName InName)
{
PRAGMA_DISABLE_DEPRECATION_WARNINGS  // Until Outputs becomes private
	for (TPair<int32, FDataflowOutput*>& Elem : Outputs)
PRAGMA_ENABLE_DEPRECATION_WARNINGS
	{
		FDataflowOutput* const Con = Elem.Value;
		if (Con->GetName().IsEqual(InName))
		{
			return Con;
		}
	}
	return nullptr;
}

const FDataflowOutput* FDataflowNode::FindOutput(FName InName) const
{
PRAGMA_DISABLE_DEPRECATION_WARNINGS  // Until Outputs becomes private
	for (const TPair<int32, FDataflowOutput*>& Elem : Outputs)
PRAGMA_ENABLE_DEPRECATION_WARNINGS
	{
		const FDataflowOutput* const Con = Elem.Value;
		if (Con->GetName().IsEqual(InName))
		{
			return Con;
		}
	}
	return nullptr;
}

const FDataflowOutput* FDataflowNode::FindOutput(uint32 InGuidHash) const
{
PRAGMA_DISABLE_DEPRECATION_WARNINGS  // Until Outputs becomes private
	for (const TPair<int32, FDataflowOutput*>& Elem : Outputs)
PRAGMA_ENABLE_DEPRECATION_WARNINGS
	{
		const FDataflowOutput* const Con = Elem.Value;
		if (GetTypeHash(Con->GetGuid()) == InGuidHash)
		{
			return Con;
		}
	}
	return nullptr;
}

const FDataflowOutput* FDataflowNode::FindOutput(const UE::Dataflow::FConnectionKey& Key) const
{
PRAGMA_DISABLE_DEPRECATION_WARNINGS  // Until Outputs becomes private
	if (const FDataflowOutput* const* Con = Outputs.Find(Key.Offset))
PRAGMA_ENABLE_DEPRECATION_WARNINGS
	{
		check(*Con);
		return *Con;
	}
	return nullptr;
}

const FDataflowOutput* FDataflowNode::FindOutput(const UE::Dataflow::FConnectionReference& Reference) const
{
	const UE::Dataflow::FConnectionKey Key = GetKeyFromReference(Reference);
	if (const FDataflowOutput* const Con = FindOutput(Key))
	{
		check(Con->RealAddress() == Reference.Reference);
		return Con;
	}
	return nullptr;
}

FDataflowOutput* FDataflowNode::FindOutput(const UE::Dataflow::FConnectionKey& Key)
{
PRAGMA_DISABLE_DEPRECATION_WARNINGS  // Until Outputs becomes private
	if (FDataflowOutput* const* Con = Outputs.Find(Key.Offset))
PRAGMA_ENABLE_DEPRECATION_WARNINGS
	{
		check(*Con);
		return *Con;
	}
	return nullptr;
}

FDataflowOutput* FDataflowNode::FindOutput(const UE::Dataflow::FConnectionReference& Reference)
{
	const UE::Dataflow::FConnectionKey Key = GetKeyFromReference(Reference);
	if (FDataflowOutput* const Con = FindOutput(Key))
	{
		check(Con->RealAddress() == Reference.Reference);
		return Con;
	}
	return nullptr;
}

const FDataflowOutput* FDataflowNode::FindOutput(const FGuid& InGuid) const
{
PRAGMA_DISABLE_DEPRECATION_WARNINGS  // Until Outputs becomes private
	for (const TPair<int32, FDataflowOutput*>& Elem : Outputs)
PRAGMA_ENABLE_DEPRECATION_WARNINGS
	{
		const FDataflowOutput* const Con = Elem.Value;
		if (Con->GetGuid() == InGuid)
		{
			return Con;
		}
	}
	return nullptr;
}

int32 FDataflowNode::NumOutputs() const
{
PRAGMA_DISABLE_DEPRECATION_WARNINGS  // Until Outputs becomes private
	return Outputs.Num();
PRAGMA_ENABLE_DEPRECATION_WARNINGS
}


TArray< FDataflowOutput* > FDataflowNode::GetOutputs() const
{
	TArray< FDataflowOutput* > Result;
PRAGMA_DISABLE_DEPRECATION_WARNINGS  // Until Outputs becomes private
	Outputs.GenerateValueArray(Result);
PRAGMA_ENABLE_DEPRECATION_WARNINGS
	return Result;
}


void FDataflowNode::ClearOutputs()
{
PRAGMA_DISABLE_DEPRECATION_WARNINGS  // Until Outputs becomes private
	for (TPair<int32, FDataflowOutput*>& Elem : Outputs)
PRAGMA_ENABLE_DEPRECATION_WARNINGS
	{
		FDataflowOutput* const Con = Elem.Value;
		delete Con;
	}
PRAGMA_DISABLE_DEPRECATION_WARNINGS  // Until Outputs becomes private
	Outputs.Reset();
PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

bool FDataflowNode::HasHideableOutputs() const
{
PRAGMA_DISABLE_DEPRECATION_WARNINGS  // Until Outputs becomes private
	for (const TPair<int32, FDataflowOutput*>& Elem : Outputs)
PRAGMA_ENABLE_DEPRECATION_WARNINGS
	{
		const FDataflowOutput* const Con = Elem.Value;
		if (Con->GetCanHidePin())
		{
			return true;
		}
	}
	return false;
}

bool FDataflowNode::HasHiddenOutputs() const
{
PRAGMA_DISABLE_DEPRECATION_WARNINGS  // Until Outputs becomes private
	for (const TPair<int32, FDataflowOutput*>& Elem : Outputs)
PRAGMA_ENABLE_DEPRECATION_WARNINGS
	{
		const FDataflowOutput* const Con = Elem.Value;
		if (Con->GetPinIsHidden())
		{
			return true;
		}
	}
	return false;
}

TArray<UE::Dataflow::FPin> FDataflowNode::GetPins() const
{
	TArray<UE::Dataflow::FPin> RetVal;
	for (const TPair<UE::Dataflow::FConnectionKey, FDataflowInput*>& Elem : ExpandedInputs)
	{
		const FDataflowInput* const Con = Elem.Value;
		RetVal.Add({ UE::Dataflow::FPin::EDirection::INPUT,Con->GetType(), Con->GetName(), Con->GetPinIsHidden()});
	}
PRAGMA_DISABLE_DEPRECATION_WARNINGS  // Until Outputs becomes private
	for (const TPair<int32, FDataflowOutput*>& Elem : Outputs)
PRAGMA_ENABLE_DEPRECATION_WARNINGS
	{
		const FDataflowOutput* const Con = Elem.Value;
		RetVal.Add({ UE::Dataflow::FPin::EDirection::OUTPUT,Con->GetType(), Con->GetName(), Con->GetPinIsHidden() });
	}
	return RetVal;
}

void FDataflowNode::UnregisterPinConnection(const UE::Dataflow::FPin& Pin)
{
	if (Pin.Direction == UE::Dataflow::FPin::EDirection::INPUT)
	{
		for (TMap<UE::Dataflow::FConnectionKey, FDataflowInput*>::TIterator Iter = ExpandedInputs.CreateIterator(); Iter; ++Iter)
		{
			FDataflowInput* Con = Iter.Value();
			if (Con->GetName().IsEqual(Pin.Name) && Con->GetType().IsEqual(Pin.Type))
			{
				Iter.RemoveCurrent();
				delete Con;

				// Invalidate graph as this input might have had connections
				Invalidate();
				break;
			}
		}
	}
	else if (Pin.Direction == UE::Dataflow::FPin::EDirection::OUTPUT)
	{
PRAGMA_DISABLE_DEPRECATION_WARNINGS  // Until Outputs becomes private
		for (TMap<int32, FDataflowOutput*>::TIterator Iter = Outputs.CreateIterator(); Iter; ++Iter)
PRAGMA_ENABLE_DEPRECATION_WARNINGS
		{
			FDataflowOutput* Con = Iter.Value();
			if (Con->GetName().IsEqual(Pin.Name) && Con->GetType().IsEqual(Pin.Type))
			{
				Iter.RemoveCurrent();
				delete Con;

				// Invalidate graph as this input might have had connections
				Invalidate();
				break;
			}
		}
	}
}

void FDataflowNode::Invalidate(const UE::Dataflow::FTimestamp& InModifiedTimestamp)
{
	if (bPauseInvalidations)
	{
		if (PausedModifiedTimestamp < InModifiedTimestamp)
		{
			PausedModifiedTimestamp = InModifiedTimestamp;
		}
		return;
	}
PRAGMA_DISABLE_DEPRECATION_WARNINGS  // Until LastModifiedTimestamp becomes private
	if (LastModifiedTimestamp < InModifiedTimestamp)
	{
		LastModifiedTimestamp = InModifiedTimestamp;
		for (TPair<int32, FDataflowOutput*>& Elem : Outputs)
PRAGMA_ENABLE_DEPRECATION_WARNINGS
		{
			FDataflowOutput* const Con = Elem.Value;
			Con->Invalidate(InModifiedTimestamp);
		}

		OnInvalidate();

		OnNodeInvalidatedDelegate.Broadcast(this);
	}
}

const FProperty* FDataflowNode::FindProperty(const UStruct* Struct, const void* InProperty, const FName& PropertyName, TArray<const FProperty*>* OutPropertyChain) const
{
	return UE::Dataflow::Private::FindProperty(Struct, this, InProperty, PropertyName, OutPropertyChain);
}

const FProperty& FDataflowNode::FindPropertyChecked(const UStruct* Struct, const void* InProperty, const FName& PropertyName, TArray<const FProperty*>* OutPropertyChain) const
{
	return UE::Dataflow::Private::FindPropertyChecked(Struct, this, InProperty, PropertyName, OutPropertyChain);
}

const FProperty* FDataflowNode::FindProperty(const UStruct* Struct, const FName& PropertyFullName, TArray<const FProperty*>* OutPropertyChain) const
{
	// If PropertyFullName corresponds with an array property, it will contain a [ContainerIndex]. We don't care about which element in the array we're in--the FProperty will be the same.
	const FString PropertyFullNameStringIndexNone = StripContainerIndexFromPropertyFullName(PropertyFullName.ToString());

	const FProperty* Property = nullptr;
	for (FPropertyValueIterator PropertyIt(FProperty::StaticClass(), Struct, this); PropertyIt; ++PropertyIt)
	{
		TArray<const FProperty*> PropertyChain;
		PropertyIt.GetPropertyChain(PropertyChain);
		if (GetPropertyFullNameString(PropertyChain, INDEX_NONE) == PropertyFullNameStringIndexNone)
		{
			Property = PropertyIt.Key();
			if (OutPropertyChain)
			{
				*OutPropertyChain = MoveTemp(PropertyChain);
			}
			break;
		}
	}
	return Property;
}

uint32 FDataflowNode::GetPropertyOffset(const TArray<const FProperty*>& PropertyChain)
{
	uint32 Offset = 0;
	for (const FProperty* const Property : PropertyChain)
	{
		Offset += (uint32)Property->GetOffset_ForInternal();
	}
	return Offset;
}

uint32 FDataflowNode::GetPropertyOffset(const FName& PropertyFullName) const
{
	uint32 Offset = 0;
	if (const TUniquePtr<const FStructOnScope> ScriptOnStruct =
		TUniquePtr<FStructOnScope>(const_cast<FDataflowNode*>(this)->NewStructOnScope()))  // The mutable Struct Memory is not accessed here, allowing for the const_cast and keeping this method const
	{
		if (const UStruct* const Struct = ScriptOnStruct->GetStruct())
		{
			TArray<const FProperty*> PropertyChain;
			FindProperty(Struct, PropertyFullName, &PropertyChain);
			Offset = GetPropertyOffset(PropertyChain);
		}
	}
	return Offset;
}

uint32 FDataflowNode::GetConnectionOffsetFromReference(const void* Reference) const
{
	return (uint32)((size_t)Reference - (size_t)this);
}

UE::Dataflow::FConnectionKey FDataflowNode::GetKeyFromReference(const UE::Dataflow::FConnectionReference& Reference) const
{
	UE::Dataflow::FConnectionKey Key;
	Key.Offset = Reference.ContainerReference ? GetConnectionOffsetFromReference(Reference.ContainerReference) : GetConnectionOffsetFromReference(Reference.Reference);
	Key.ContainerIndex = Reference.Index;
	Key.ContainerElementOffset = INDEX_NONE;
	if (const FArrayProperty* const* ArrayProperty = InputArrayProperties.Find(Key.Offset))
	{
		Key.ContainerElementOffset = UE::Dataflow::Private::GetArrayElementOffsetFromReference(*ArrayProperty, Reference);
	}
	return Key;
}

FString FDataflowNode::GetPropertyFullNameString(const TConstArrayView<const FProperty*>& PropertyChain, int32 ContainerIndex)
{
	FString PropertyFullName;
	bool bFoundArrayProperty = false;
	for (int32 Index = PropertyChain.Num()-1; Index >= 0 ; --Index)
	{
		const FProperty* const Property = PropertyChain[Index];
		FString PropertyName = Property->GetName();
		if (const FArrayProperty* const ArrayProperty = CastField<FArrayProperty>(Property))
		{
			if (ContainerIndex != INDEX_NONE)
			{
				check(!bFoundArrayProperty); // We only expect to find one array to substitute in.
				bFoundArrayProperty = true;
				PropertyName = FString::Format(TEXT("{0}[{1}]"), { PropertyName, ContainerIndex });
			}

			// Skip the next property. It has the same name as the container (e.g., otherwise you'll get MyFloatArray[5].MyFloatArray)
			--Index;
		}

		PropertyFullName = PropertyFullName.IsEmpty() ?
			PropertyName :
			FString::Format(TEXT("{0}.{1}"), { PropertyFullName, PropertyName });
	}
	return PropertyFullName;
}

FName FDataflowNode::GetPropertyFullName(const TArray<const FProperty*>& PropertyChain, int32 ContainerIndex)
{
	const FString PropertyFullName = GetPropertyFullNameString(TConstArrayView<const FProperty*>(PropertyChain), ContainerIndex);
	return FName(*PropertyFullName);
}

FString FDataflowNode::StripContainerIndexFromPropertyFullName(const FString& InPropertyFullName)
{
	FString PropertyFullName = InPropertyFullName;
	FString PropertyFullNameStripped;

	int32 OpenSquareBracketIndex, CloseSquareBracketIndex;
	while((PropertyFullName.FindChar('[', OpenSquareBracketIndex) && PropertyFullName.FindChar(']', CloseSquareBracketIndex)
		&& OpenSquareBracketIndex < CloseSquareBracketIndex))
	{
		if (CloseSquareBracketIndex > OpenSquareBracketIndex + 1 && PropertyFullName.Mid(OpenSquareBracketIndex + 1, CloseSquareBracketIndex - OpenSquareBracketIndex - 1).IsNumeric())
		{
			// number within brackets. remove it.
			PropertyFullNameStripped += PropertyFullName.Left(OpenSquareBracketIndex);
		}
		else
		{
			// We found some other brackets like [foo] or []. These didn't come from our ContainerIndex. Just leave them and move on.
			PropertyFullNameStripped += PropertyFullName.Left(CloseSquareBracketIndex + 1);
		}
		PropertyFullName.RightChopInline(CloseSquareBracketIndex + 1);
	}
	PropertyFullNameStripped += PropertyFullName;
	return PropertyFullNameStripped;
}

FText FDataflowNode::GetPropertyDisplayNameText(const TArray<const FProperty*>& PropertyChain, int32 ContainerIndex)
{
#if WITH_EDITORONLY_DATA  // GetDisplayNameText() is only available if WITH_EDITORONLY_DATA
	static const FTextFormat TextFormat(NSLOCTEXT("DataflowNode", "PropertyDisplayNameTextConcatenator", "{0}.{1}"));
	FText PropertyText;
	bool bIsPropertyTextEmpty = true;
	bool bFoundArrayProperty = false;
	for (int32 Index = PropertyChain.Num() - 1; Index >= 0; --Index)
	{
		const FProperty* const Property = PropertyChain[Index];
		if (!Property->HasMetaData(FName(TEXT("SkipInDisplayNameChain"))))
		{
			FText PropertyDisplayName = Property->GetDisplayNameText();
			PropertyText = bIsPropertyTextEmpty ?
				MoveTemp(PropertyDisplayName) :
				FText::Format(TextFormat, PropertyText, MoveTemp(PropertyDisplayName));
			bIsPropertyTextEmpty = false;
		}
		if (const FArrayProperty* const ArrayProperty = CastField<FArrayProperty>(Property))
		{
			check(!bFoundArrayProperty); // We only expect to find one array to substitute in.
			bFoundArrayProperty = (ContainerIndex != INDEX_NONE);
			--Index;  // Skip ElemProperty. Otherwise you get names like "MyFloatArray[0].MyFloatArray" when you just want "MyFloatArray[0]"
		}
	}
	if (bFoundArrayProperty)
	{
		static const FTextFormat TextFormatContainer(NSLOCTEXT("DataflowNode", "PropertyDisplayNameTextContainer", "{0}[{1}]"));
		PropertyText = FText::Format(TextFormatContainer, PropertyText, ContainerIndex);
	}

	return PropertyText;
#else
	return FText::FromName(GetPropertyFullName(PropertyChain, ContainerIndex));
#endif
}

void FDataflowNode::InitConnectionParametersFromPropertyReference(const FStructOnScope& StructOnScope, const void* PropertyRef, const FName& PropertyName, UE::Dataflow::FConnectionParameters& OutParams)
{
	const UStruct* Struct = StructOnScope.GetStruct();
	check(Struct);
	TArray<const FProperty*> PropertyChain;
	const FProperty& Property = FindPropertyChecked(Struct, PropertyRef, PropertyName, &PropertyChain);
	check(PropertyChain.Num());
	FString ExtendedType;
	const FString CPPType = Property.GetCPPType(&ExtendedType);
	OutParams.Type = FName(CPPType + ExtendedType);
	OutParams.Name = GetPropertyFullName(PropertyChain);
	OutParams.Property = &Property;
	OutParams.Owner = this;
	OutParams.Offset = GetConnectionOffsetFromReference(PropertyRef);
	check(OutParams.Offset == GetPropertyOffset(PropertyChain));
}

FDataflowInput& FDataflowNode::RegisterInputConnectionInternal(const UE::Dataflow::FConnectionReference& Reference, const FName& PropertyName)
{
	TUniquePtr<FStructOnScope> ScriptOnStruct = TUniquePtr<FStructOnScope>(NewStructOnScope());
	check(ScriptOnStruct);
	UE::Dataflow::FInputParameters InputParams;
	InitConnectionParametersFromPropertyReference(*ScriptOnStruct, Reference.Reference, PropertyName, InputParams);
	FDataflowInput* const Input = new FDataflowInput(InputParams);
	check(Input->RealAddress() == Reference.Reference);
	AddInput(Input);
	check(FindInput(Reference) == Input);

	return *Input;
}

FDataflowInput& FDataflowNode::RegisterInputArrayConnectionInternal(const UE::Dataflow::FConnectionReference& Reference, const FName& ElementPropertyName,
	const FName& ArrayPropertyName)
{
	TUniquePtr<FStructOnScope> ScriptOnStruct = TUniquePtr<FStructOnScope>(NewStructOnScope());
	check(ScriptOnStruct);
	const UStruct* Struct = ScriptOnStruct->GetStruct();
	check(Struct);
	UE::Dataflow::FArrayInputParameters InputParams;
	InputParams.Owner = this;

	// Find the Array property.
	TArray<const FProperty*> ArrayPropertyChain;
	for (FPropertyValueIterator PropertyIt(FArrayProperty::StaticClass(), Struct, this); PropertyIt; ++PropertyIt)
	{
		if (Reference.ContainerReference == PropertyIt.Value() && (ArrayPropertyName == NAME_None || ArrayPropertyName == PropertyIt.Key()->GetName()))
		{
			InputParams.ArrayProperty = CastFieldChecked<FArrayProperty>(PropertyIt.Key());
			InputParams.Offset = GetConnectionOffsetFromReference(Reference.ContainerReference);
			PropertyIt.GetPropertyChain(ArrayPropertyChain);
			break;
		}
	}

	check(InputParams.ArrayProperty);

	// Find the element property
	TArray<const FProperty*> PropertyChain;
	const void* const AddressAtIndex = InputParams.ArrayProperty->GetValueAddressAtIndex_Direct(InputParams.ArrayProperty->Inner, const_cast<void*>(Reference.ContainerReference), Reference.Index);
	if (AddressAtIndex == Reference.Reference && (ElementPropertyName == NAME_None || ElementPropertyName == InputParams.ArrayProperty->Inner->GetName()))
	{
		InputParams.Property = InputParams.ArrayProperty->Inner;
		PropertyChain = { InputParams.ArrayProperty->Inner };
	}
	else if (const FStructProperty* const InnerStruct = CastField<FStructProperty>(InputParams.ArrayProperty->Inner))
	{
		InputParams.Property = &UE::Dataflow::Private::FindPropertyChecked(InnerStruct->Struct, AddressAtIndex, Reference.Reference, ElementPropertyName, &PropertyChain);
		PropertyChain.Add(InnerStruct);
	}

	check(InputParams.Property);

	PropertyChain.Append(ArrayPropertyChain);
	FString ExtendedType;
	const FString CPPType = InputParams.Property->GetCPPType(&ExtendedType);
	InputParams.Type = FName(CPPType + ExtendedType);
	InputParams.Name = GetPropertyFullName(PropertyChain, Reference.Index);
	InputParams.InnerOffset = UE::Dataflow::Private::GetArrayElementOffsetFromReference(InputParams.ArrayProperty, Reference);

	InputArrayProperties.Emplace(InputParams.Offset, InputParams.ArrayProperty);

	FDataflowInput* const Input = new FDataflowArrayInput(Reference.Index, InputParams);
	AddInput(Input);
	check(FindInput(Reference) == Input);
	return *Input;
}

void FDataflowNode::UnregisterInputConnection(const UE::Dataflow::FConnectionKey& Key)
{
	if (ExpandedInputs.Remove(Key))
	{
		// Invalidate graph as this input might have had connections
		Invalidate();
	}
}

FDataflowOutput& FDataflowNode::RegisterOutputConnectionInternal(const UE::Dataflow::FConnectionReference& Reference, const FName& PropertyName)
{
	TUniquePtr<FStructOnScope> ScriptOnStruct = TUniquePtr<FStructOnScope>(NewStructOnScope());
	check(ScriptOnStruct);
	UE::Dataflow::FOutputParameters OutputParams;
	InitConnectionParametersFromPropertyReference(*ScriptOnStruct, Reference.Reference, PropertyName, OutputParams);
	FDataflowOutput* OutputConnection = new FDataflowOutput(OutputParams);
	check(OutputConnection->RealAddress() == Reference.Reference);

	AddOutput(OutputConnection);
	check(FindOutput(Reference) == OutputConnection);
	check(FindOutput(OutputConnection->GetConnectionKey()) == OutputConnection);
	return *OutputConnection;
}

uint32 FDataflowNode::GetValueHash()
{
	//UE_LOG(LogChaos, Warning, TEXT("%s"), *GetName().ToString())

	uint32 Hash = 0;
	if (const TUniquePtr<FStructOnScope> ScriptOnStruct = TUniquePtr<FStructOnScope>(NewStructOnScope()))
	{
		if (const UStruct* const Struct = ScriptOnStruct->GetStruct())
		{
			for (FPropertyValueIterator PropertyIt(FProperty::StaticClass(), Struct, this); PropertyIt; ++PropertyIt)
			{
				if (const FProperty* const Property = PropertyIt.Key())
				{
					if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
					{
						//
						// Note : [CacheContextPropertySupport]
						// 
						// Some UPROPERTIES do not support hash values.For example, FFilePath, is a struct 
						// that is not defined using USTRUCT, and does not support the GetTypeValue() function.
						// These types of attributes need to return a Zero(0) hash, to indicate that the Hash 
						// is not supported.To add property hashing support, add GetTypeValue to the properties 
						// supporting USTRUCT(See Class.h  UScriptStruct::GetStructTypeHash)
						// 
						if (!StructProperty->Struct) return 0;
						if (!StructProperty->Struct->GetCppStructOps()) return 0;
					}

					if (Property->PropertyFlags & CPF_HasGetValueTypeHash)
					{
						// uint32 CrcHash = FCrc::MemCrc32(PropertyIt.Value(), Property->ElementSize);
						// UE_LOG(LogChaos, Warning, TEXT("( %lu \t%s"), (unsigned long)CrcHash, *Property->GetName())

						if (Property->PropertyFlags & CPF_TObjectPtr)
						{
							// @todo(dataflow) : Do something about TObjectPtr<T>
						}
						else
						{
							Hash = HashCombine(Hash, Property->GetValueTypeHash(PropertyIt.Value()));
						}
					}
				}
			}
		}
	}
	return Hash;
}

void FDataflowNode::ValidateProperties()
{
	if (const TUniquePtr<FStructOnScope> ScriptOnStruct = TUniquePtr<FStructOnScope>(NewStructOnScope()))
	{
		if (const UStruct* const Struct = ScriptOnStruct->GetStruct())
		{
			for (FPropertyValueIterator PropertyIt(FProperty::StaticClass(), Struct, this); PropertyIt; ++PropertyIt)
			{
				if (const FProperty* const Property = PropertyIt.Key())
				{
					if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
					{
						if (!StructProperty->Struct || !StructProperty->Struct->GetCppStructOps())
						{
							// See Note : [CacheContextPropertySupport]
							FString StructPropertyName;
							StructProperty->GetName(StructPropertyName);
							UE_LOG(LogChaos, Warning, 
								TEXT("Dataflow: Context caching disable for graphs with node '%s' due to non-hashed UPROPERTY '%s'."), 
								*GetName().ToString(), *StructPropertyName)
						}
					}
				}
			}
		}
	}
}

bool FDataflowNode::ValidateConnections()
{
	bHasValidConnections = true;
#if WITH_EDITORONLY_DATA
	if (const TUniquePtr<FStructOnScope> ScriptOnStruct = TUniquePtr<FStructOnScope>(NewStructOnScope()))
	{
		if (const UStruct* const Struct = ScriptOnStruct->GetStruct())
		{
			for (FPropertyValueIterator PropertyIt(FProperty::StaticClass(), Struct, ScriptOnStruct->GetStructMemory()); PropertyIt; ++PropertyIt)
			{
				const FProperty* const Property = PropertyIt.Key();
				check(Property);
				TArray<const FProperty*> PropertyChain;
				PropertyIt.GetPropertyChain(PropertyChain);
				const FName PropName(GetPropertyFullName(PropertyChain));

				if (Property->HasMetaData(FDataflowNode::DataflowInput))
				{
					if (!FindInput(PropertyIt.Value()))
					{
						ensure(false);
						UE_LOG(LogChaos, Warning, TEXT("Missing dataflow RegisterInputConnection in constructor for (%s:%s)"), *GetName().ToString(), *PropName.ToString())
							bHasValidConnections = false;
					}
				}
				if (Property->HasMetaData(FDataflowNode::DataflowOutput))
				{
					const FDataflowOutput* const OutputConnection = FindOutput(PropertyIt.Value());
					if(!OutputConnection)
					{
						ensure(false);
						UE_LOG(LogChaos, Warning, TEXT("Missing dataflow RegisterOutputConnection in constructor for (%s:%s)"), *GetName().ToString(),*PropName.ToString());
						bHasValidConnections = false;
					}

					// If OutputConnection is valid, validate passthrough connections if they exist
					else if (const FString* PassthroughName = Property->FindMetaData(FDataflowNode::DataflowPassthrough))
					{
						// Assume passthrough name is relative to current property name.
						FString FullPassthroughName;
						if (PropertyChain.Num() <= 1)
						{
							FullPassthroughName = *PassthroughName;
						}
						else
						{
							FullPassthroughName = FString::Format(TEXT("{0}.{1}"), { GetPropertyFullNameString(TConstArrayView<const FProperty*>(&PropertyChain[1], PropertyChain.Num() - 1)), *PassthroughName });
						}

						const FDataflowInput* const PassthroughConnectionInput = OutputConnection->GetPassthroughInput();
						if (PassthroughConnectionInput == nullptr)
						{
							ensure(false);
							UE_LOG(LogChaos, Warning, TEXT("Missing DataflowPassthrough registration for (%s:%s)"), *GetName().ToString(), *PropName.ToString());
							bHasValidConnections = false;
						}

						const FDataflowInput* const PassthroughConnectionInputFromMetadata = FindInput(FName(FullPassthroughName));

						if(PassthroughConnectionInput != PassthroughConnectionInputFromMetadata)
						{
							ensure(false);
							UE_LOG(LogChaos, Warning, TEXT("Mismatch in declared and registered DataflowPassthrough connection; (%s:%s vs %s)"), *GetName().ToString(), *FullPassthroughName, *PassthroughConnectionInput->GetName().ToString());
							bHasValidConnections = false;
						}

						if(!PassthroughConnectionInputFromMetadata)
						{
							ensure(false);
							UE_LOG(LogChaos, Warning, TEXT("Incorrect DataflowPassthrough Connection set for (%s:%s)"), *GetName().ToString(), *PropName.ToString());
							bHasValidConnections = false;
						}

						else if(OutputConnection->GetType() != PassthroughConnectionInput->GetType())
						{
							ensure(false);
							UE_LOG(LogChaos, Warning, TEXT("DataflowPassthrough connection types mismatch for (%s:%s)"), *GetName().ToString(), *PropName.ToString());
							bHasValidConnections = false;
						}
					}
					else if(OutputConnection->GetPassthroughInput()) 
					{
						ensure(false);
						UE_LOG(LogChaos, Warning, TEXT("Missing DataflowPassthrough declaration for (%s:%s)"), *GetName().ToString(), *PropName.ToString());
						bHasValidConnections = false;
					}
				}
			}

#if 0
		 // disabling this out this for now as this fail all over the place for some dataflow graphs 
		 // we may get rid of the metadata constraints we may not need it anymore ( to be decided later )
			for (const TPair<UE::Dataflow::FConnectionKey, FDataflowInput*>& ExpandedInput : ExpandedInputs)
			{
				const FDataflowInput* Input = ExpandedInput.Value;

				if (const FProperty* Property = Input->GetProperty())
				{
					if (!Property->HasMetaData(FDataflowNode::DataflowInput))
					{
						ensure(false);
						UE_LOG(LogChaos, Warning, TEXT("Missing dataflow DataflowInput declaration for (%s:%s)"), *GetName().ToString(), *Property->GetFName().ToString());
					}
				}
			}

PRAGMA_DISABLE_DEPRECATION_WARNINGS  // Until Outputs becomes private
			for (const TPair<int, FDataflowOutput*>& Output : Outputs)
PRAGMA_ENABLE_DEPRECATION_WARNINGS
			{
				FDataflowOutput* OutputVal = Output.Value;

				if (const FProperty* Property = OutputVal->GetProperty())
				{
					if (!Property->HasMetaData(FDataflowNode::DataflowOutput))
					{
						ensure(false);
						UE_LOG(LogChaos, Warning, TEXT("Missing dataflow DataflowOutput declaration for (%s:%s)"), *GetName().ToString(), *Property->GetFName().ToString());
					}
				}
			}
#endif	
		}
	}
#endif
	return bHasValidConnections;
}

TUniquePtr<const FStructOnScope> FDataflowNode::NewStructOnScopeConst() const
{
	// New StructOnScopeConst is non-const and virtual , changing it would be quite difficult
	// we have to use a const cast though to accomodate this current constraint
	// but we make sure to return a const version of the FStructOnScope 
	return TUniquePtr<const FStructOnScope>(const_cast<FDataflowNode*>(this)->NewStructOnScope());
}

FString FDataflowNode::GetToolTip() const
{
	UE::Dataflow::FFactoryParameters FactoryParameters = UE::Dataflow::FNodeFactory::GetInstance()->GetParameters(GetType());

	return FactoryParameters.ToolTip;
}

FText FDataflowNode::GetPinDisplayName(const FName& PropertyFullName, const UE::Dataflow::FPin::EDirection Direction) const
{
	int32 ContainerIndex = INDEX_NONE;

	if (Direction == UE::Dataflow::FPin::EDirection::INPUT)
	{
		if (const FDataflowInput* const Input = FindInput(PropertyFullName))
		{
			ContainerIndex = Input->GetContainerIndex();
		}
	}
	else if (Direction == UE::Dataflow::FPin::EDirection::OUTPUT)
	{
		if (const FDataflowOutput* const Output = FindOutput(PropertyFullName))
		{
			ContainerIndex = Output->GetContainerIndex();
		}
	}

	if (const TUniquePtr<const FStructOnScope> ScriptOnStruct = NewStructOnScopeConst())
	{
		if (const UStruct* const Struct = ScriptOnStruct->GetStruct())
		{
			TArray<const FProperty*> PropertyChain;
			if (FindProperty(Struct, PropertyFullName, &PropertyChain))
			{
				return GetPropertyDisplayNameText(PropertyChain, ContainerIndex);
			}
		}
	}

	return FText();
}

FString FDataflowNode::GetPinToolTip(const FName& PropertyFullName, const UE::Dataflow::FPin::EDirection Direction) const
{
#if WITH_EDITORONLY_DATA
	if (Direction == UE::Dataflow::FPin::EDirection::INPUT)
	{
		if (const FDataflowInput* const Input = FindInput(PropertyFullName))
		{
			if (const FProperty* const Property = Input->GetProperty())
			{
				return UE::Dataflow::Private::GetPinToolTipFromProperty(Property);
			}
		}
	}
	else if (Direction == UE::Dataflow::FPin::EDirection::OUTPUT)
	{
		if (const FDataflowOutput* const Output = FindOutput(PropertyFullName))
		{
			if (const FProperty* const Property = Output->GetProperty())
			{
				return UE::Dataflow::Private::GetPinToolTipFromProperty(Property);
			}
		}
	}
	else if (const TUniquePtr<const FStructOnScope> ScriptOnStruct = NewStructOnScopeConst())
	{
		if (const UStruct* const Struct = ScriptOnStruct->GetStruct())
		{
			if (const FProperty* const Property = FindProperty(Struct, PropertyFullName))
			{
				return UE::Dataflow::Private::GetPinToolTipFromProperty(Property);
			}
		}
	}
#endif

	return {};
}

TArray<FString> FDataflowNode::GetPinMetaData(const FName& PropertyFullName, const UE::Dataflow::FPin::EDirection Direction) const
{
#if WITH_EDITORONLY_DATA
	if (Direction == UE::Dataflow::FPin::EDirection::INPUT)
	{
		if (const FDataflowInput* const Input = FindInput(PropertyFullName))
		{
			if (const FProperty* const Property = Input->GetProperty())
			{
				return UE::Dataflow::Private::GetPinMetaDataFromProperty(Property);
			}
		}
	}
	else if (Direction == UE::Dataflow::FPin::EDirection::OUTPUT)
	{
		if (const FDataflowOutput* const Output = FindOutput(PropertyFullName))
		{
			if (const FProperty* const Property = Output->GetProperty())
			{
				return UE::Dataflow::Private::GetPinMetaDataFromProperty(Property);
			}
		}
	}
	else if (const TUniquePtr<const FStructOnScope> ScriptOnStruct = NewStructOnScopeConst())
	{
		if (const UStruct* const Struct = ScriptOnStruct->GetStruct())
		{
			if (const FProperty* const Property = FindProperty(Struct, PropertyFullName))
			{
				return UE::Dataflow::Private::GetPinMetaDataFromProperty(Property);
			}
		}
	}
#endif

	return TArray<FString>();
}

void FDataflowNode::CopyNodeProperties(const TSharedPtr<FDataflowNode> CopyFromDataflowNode)
{
	TArray<uint8> NodeData;

	FObjectWriter ArWriter(NodeData);
	CopyFromDataflowNode->SerializeInternal(ArWriter);

	FObjectReader ArReader(NodeData);
	this->SerializeInternal(ArReader);
}

void FDataflowNode::ForwardInput(UE::Dataflow::FContext& Context, const UE::Dataflow::FConnectionReference& InputReference, const UE::Dataflow::FConnectionReference& Reference) const
{
	if (const FDataflowOutput* Output = FindOutput(Reference))
	{
		if (const FDataflowInput* Input = FindInput(InputReference))
		{
			// we need to pull the value first so the upstream of the graph evaluate 
			Input->PullValue(Context);
			Output->ForwardInput(Input, Context);
		}
		else
		{
			checkfSlow(false, TEXT("This input could not be found within this node, check this has been properly registered in the node constructor"));
		}
	}
	else
	{
		checkfSlow(false, TEXT("This output could not be found within this node, check this has been properly registered in the node constructor"));
	}
}

bool FDataflowNode::TrySetConnectionType(FDataflowConnection* Connection, FName NewType)
{
	if (Connection)
	{
		if (Connection->IsAnyType() && Connection->GetType() != NewType && !FDataflowConnection::IsAnyType(NewType))
		{
			Connection->SetConcreteType(NewType);
			NotifyConnectionTypeChanged(Connection);
			return true;
		}
	}
	return false;
}

void FDataflowNode::NotifyConnectionTypeChanged(FDataflowConnection* Connection)
{
	if (Connection->IsAnyType())
	{
		if (Connection->GetDirection() == UE::Dataflow::FPin::EDirection::INPUT)
		{
			OnInputTypeChanged((FDataflowInput*)Connection);
		}
		if (Connection->GetDirection() == UE::Dataflow::FPin::EDirection::OUTPUT)
		{
			OnOutputTypeChanged((FDataflowOutput*)Connection);
		}
	}
}

bool FDataflowNode::SetInputConcreteType(const UE::Dataflow::FConnectionReference& InputReference, FName NewType)
{
	if (FDataflowInput* Input = FindInput(InputReference))
	{
		if (Input->GetType() != NewType)
		{
			return Input->SetConcreteType(NewType);
		}
	}
	return false;
}

bool FDataflowNode::SetOutputConcreteType(const UE::Dataflow::FConnectionReference& OutputReference, FName NewType)
{
	if (FDataflowOutput* Output = FindOutput(OutputReference))
	{
		if (Output->GetType() != NewType)
		{
			return Output->SetConcreteType(NewType);
		}
	}
	return false;
}

bool FDataflowNode::SetAllConnectionConcreteType(FName NewType)
{
	bool bChanged = false;
	for (const TPair<UE::Dataflow::FConnectionKey, FDataflowInput*>& InputEntry : ExpandedInputs)
	{
		FDataflowInput* const Input = InputEntry.Value;
		if (Input && Input->GetType() != NewType)
		{
			bChanged |= Input->SetConcreteType(NewType);
		}
	}

	PRAGMA_DISABLE_DEPRECATION_WARNINGS  // Until Outputs becomes private
	for (const TPair<int32, FDataflowOutput*>& OutputEntry : Outputs)
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
	{
		FDataflowOutput* const Output = OutputEntry.Value;
		if (Output && Output->GetType() != NewType)
		{
			bChanged |= Output->SetConcreteType(NewType);
		}
	}

	return bChanged;
}