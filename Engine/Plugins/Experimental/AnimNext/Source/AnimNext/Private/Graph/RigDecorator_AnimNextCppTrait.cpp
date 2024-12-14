// Copyright Epic Games, Inc. All Rights Reserved.

#include "Graph/RigDecorator_AnimNextCppTrait.h"

#if WITH_EDITOR
#include "RigVMModel/RigVMController.h"
#include "TraitCore/TraitRegistry.h"
#endif

#include UE_INLINE_GENERATED_CPP_BY_NAME(RigDecorator_AnimNextCppTrait)

#if WITH_EDITOR
void FRigDecorator_AnimNextCppDecorator::GetProgrammaticPins(URigVMController* InController, int32 InParentPinIndex, const FString& InDefaultValue, FRigVMPinInfoArray& OutPinArray) const
{
	if (DecoratorSharedDataStruct == nullptr)
	{
		return;
	}

	FStructOnScope OriginalValueMemoryScope(DecoratorSharedDataStruct);
	FStructOnScope DefaultValueMemoryScope(DecoratorSharedDataStruct);

	if (!InDefaultValue.IsEmpty())
	{
		FRigVMPinDefaultValueImportErrorContext ErrorPipe;
		DecoratorSharedDataStruct->ImportText(*InDefaultValue, DefaultValueMemoryScope.GetStructMemory(), nullptr, PPF_SerializedAsImportText, &ErrorPipe, DecoratorSharedDataStruct->GetName());
	}

	const TFunction<ERigVMPinDefaultValueType(const FName&)> DefaultValueTypeGetter = [&OriginalValueMemoryScope, &DefaultValueMemoryScope](const FName& InPropertyName)
	{
		if(const FProperty* Property = OriginalValueMemoryScope.GetStruct()->FindPropertyByName(InPropertyName))
		{
			if(Property->Identical_InContainer(OriginalValueMemoryScope.GetStructMemory(), DefaultValueMemoryScope.GetStructMemory()))
			{
				return ERigVMPinDefaultValueType::Unset;
			}
			return ERigVMPinDefaultValueType::Override;
		}
		return ERigVMPinDefaultValueType::AutoDetect;
	};

	const int32 StartPinIndex = OutPinArray.Num();
	OutPinArray.AddPins(DecoratorSharedDataStruct, InController, ERigVMPinDirection::Invalid, InParentPinIndex, DefaultValueTypeGetter, DefaultValueMemoryScope.GetStructMemory(), true);

	for (int32 PinIndex = StartPinIndex; PinIndex < OutPinArray.Num(); ++PinIndex)
	{
		FRigVMPinInfo& PinInfo = OutPinArray[PinIndex];

		if (PinInfo.Property == nullptr)
		{
			// This pin doesn't have a property, we'll have to assume that it has been fully specified by the trait
			continue;
		}

		const bool bIsInline = PinInfo.Property->HasMetaData("Inline");
		const bool bIsTraitHandle = PinInfo.Property->GetCPPType() == TEXT("FAnimNextTraitHandle");

		// Trait handle pins are never hidden because we need to still be able to link things to it
		// UI display will use the hidden property if specified
		const bool bIsHidden = bIsTraitHandle ? false : PinInfo.Property->HasMetaData(FRigVMStruct::HiddenMetaName);

		// Check if the metadata stipulates that we should explicitly hide this property, if not we mark it as an input
		PinInfo.Direction = bIsHidden ? ERigVMPinDirection::Hidden : ERigVMPinDirection::Input;

		// For top level properties of traits, if we don't explicitly tag the property as inline or hidden, it is lazy
		// Except for trait handles which are never lazy since they just encode graph connectivity
		if (InParentPinIndex == PinInfo.ParentIndex && !bIsHidden && !bIsInline && !bIsTraitHandle)
		{
			PinInfo.bIsLazy = true;
		}

		// Remove our property because we configure the pin explicitly
		PinInfo.Property = nullptr;
	}
}

const UE::AnimNext::FTrait* FRigDecorator_AnimNextCppDecorator::GetTrait() const
{
	return UE::AnimNext::FTraitRegistry::Get().Find(DecoratorSharedDataStruct);
}
#endif
