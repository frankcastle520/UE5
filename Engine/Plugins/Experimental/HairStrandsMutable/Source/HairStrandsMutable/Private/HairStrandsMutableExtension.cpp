// Copyright Epic Games, Inc. All Rights Reserved.

#include "HairStrandsMutableExtension.h"

#include "Algo/AnyOf.h"
#include "GroomComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "MuCO/CustomizableObject.h"
#include "MuCO/CustomizableObjectInstance.h"
#include "MuCO/CustomizableObjectInstanceUsage.h"
#include "MuCO/CustomizableSkeletalComponent.h"
#include "MuCO/CustomizableSkeletalMeshActor.h"
#include "UObject/UObjectGlobals.h"

const FName GroomComponentTag(TEXT("Mutable"));

const FName UHairStrandsMutableExtension::GroomPinType(TEXT("Groom"));
const FName UHairStrandsMutableExtension::GroomsBaseNodePinName(TEXT("Grooms"));
const FText UHairStrandsMutableExtension::GroomNodeCategory(FText::FromString(TEXT("Grooms")));

TArray<FCustomizableObjectPinType> UHairStrandsMutableExtension::GetPinTypes() const
{
	TArray<FCustomizableObjectPinType> Result;
	
	FCustomizableObjectPinType& GroomType = Result.AddDefaulted_GetRef();
	GroomType.Name = GroomPinType;
	GroomType.DisplayName = FText::FromString(TEXT("Groom"));
	GroomType.Color = FLinearColor::Red;

	return Result;
}

TArray<FObjectNodeInputPin> UHairStrandsMutableExtension::GetAdditionalObjectNodePins() const
{
	TArray<FObjectNodeInputPin> Result;

	FObjectNodeInputPin& GroomInputPin = Result.AddDefaulted_GetRef();
	GroomInputPin.PinType = GroomPinType;
	GroomInputPin.PinName = GroomsBaseNodePinName;
	GroomInputPin.DisplayName = FText::FromString(TEXT("Groom"));
	GroomInputPin.bIsArray = true;

	return Result;
}

FInstancedStruct UHairStrandsMutableExtension::GenerateExtensionInstanceData(
	const TArray<FInputPinDataContainer>& InputPinData) const
{
	FInstancedStruct Result(FGroomInstanceData::StaticStruct());
	FGroomInstanceData* InstanceData = Result.GetMutablePtr<FGroomInstanceData>();

	for (const FInputPinDataContainer& Container : InputPinData)
	{
		if (Container.Pin.PinName == GroomsBaseNodePinName)
		{
			const FGroomPinData* Data = Container.Data.GetPtr<FGroomPinData>();
			if (Data
				&& Data->GroomAsset != nullptr)
			{
				InstanceData->Grooms.Add(*Data);
			}
		}
	}

	return Result;
}


void UHairStrandsMutableExtension::OnCustomizableObjectInstanceUsageUpdated(const UCustomizableObjectInstanceUsage* InstanceUsage) const
{
	UCustomizableObjectInstance* Instance = InstanceUsage->GetCustomizableObjectInstance();
	if (!Instance)
	{
		return;
	}

	USkeletalMeshComponent* AttachedParent = InstanceUsage->GetAttachParent();
	if (!AttachedParent)
	{
		return;
	}
	
	const FInstancedStruct UntypedInstanceData = Instance->GetExtensionInstanceData(this);
	const FGroomInstanceData* InstanceData = UntypedInstanceData.GetPtr<FGroomInstanceData>();
	if (!InstanceData)
	{
		return;
	}

	USceneComponent* AttachParent = InstanceUsage->GetAttachParent();
	
	TArray<UGroomComponent*> UpdateGroomComponents; // Grooms that must be present at the end of this update.

	for (const FGroomPinData& GroomPinData : InstanceData->Grooms)
	{
		if (GroomPinData.ComponentName != InstanceUsage->GetComponentName())
		{
			continue;
		}
		
		bool bAlreadyExists = false;

		for (USceneComponent* Component : AttachParent->GetAttachChildren())
		{
			UGroomComponent* GroomComponent = Cast<UGroomComponent>(Component);
			if (!GroomComponent)
			{
				continue;
			}

			if (GroomComponent->GroomAsset == GroomPinData.GroomAsset &&
				GroomComponent->GroomCache == GroomPinData.GroomCache &&
				GroomComponent->BindingAsset == GroomPinData.BindingAsset &&
				GroomComponent->PhysicsAsset == GroomPinData.PhysicsAsset &&
				GroomComponent->AttachmentName == GroomPinData.AttachmentName &&
				GroomComponent->OverrideMaterials == GroomPinData.OverrideMaterials)
			{
				UpdateGroomComponents.Add(GroomComponent);
				bAlreadyExists = true;
				break;
			}
		}

		if (bAlreadyExists)
		{
			continue;
		}
		
		UGroomComponent* GroomComponent = NewObject<UGroomComponent>(AttachParent);
		GroomComponent->GroomAsset = GroomPinData.GroomAsset;
		GroomComponent->GroomCache = GroomPinData.GroomCache;
		GroomComponent->BindingAsset = GroomPinData.BindingAsset;
		GroomComponent->PhysicsAsset = GroomPinData.PhysicsAsset;
		GroomComponent->AttachmentName = GroomPinData.AttachmentName;
		GroomComponent->OverrideMaterials = GroomPinData.OverrideMaterials;
		
		// Work around UE-158069
		GroomComponent->CreationMethod = EComponentCreationMethod::Instance;

		GroomComponent->ComponentTags.Add(GroomComponentTag);
		
		GroomComponent->AttachToComponent(AttachParent, FAttachmentTransformRules::KeepRelativeTransform);
		GroomComponent->RegisterComponent();

		UpdateGroomComponents.Add(GroomComponent);
	}

	// Destroy old Mutable grooms.
	TArray<USceneComponent*> Children;
	AttachParent->GetChildrenComponents(false, Children);
	for (USceneComponent* Child : Children)
	{
		if (Child->IsA(UGroomComponent::StaticClass()) &&
			Child->ComponentTags.Contains(GroomComponentTag) &&
			!UpdateGroomComponents.Contains(Child))
		{
			Child->DestroyComponent();
		}
	}
}
