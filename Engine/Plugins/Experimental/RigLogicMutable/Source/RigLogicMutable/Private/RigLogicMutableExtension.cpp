// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigLogicMutableExtension.h"

#include "DNAAsset.h"
#include "Engine/SkeletalMesh.h"
#include "MuCO/CustomizableObject.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/MemoryReader.h"
#include "UObject/UObjectGlobals.h"

const FName URigLogicMutableExtension::DNAPinType(TEXT("DNA"));
const FName URigLogicMutableExtension::DNABaseNodePinName(TEXT("DNA"));
const FText URigLogicMutableExtension::DNANodeCategory(FText::FromString(TEXT("DNA")));

FDNAPinData::FDNAPinData(FDNAPinData&& Source)
{
	// Invoke assignment operator
	*this = MoveTemp(Source);
}

FDNAPinData& FDNAPinData::operator=(FDNAPinData&& Source)
{
	ComponentIndex = Source.ComponentIndex;

	DNAAsset = Source.DNAAsset;
	Source.DNAAsset = nullptr;

	return *this;
}

void FDNAPinData::CopyFromDNAAsset(const UDNAAsset* SourceAsset, UObject* OuterForOwnedObjects)
{
	if (SourceAsset)
	{
		DNAAsset = URigLogicMutableExtension::CopyDNAAsset(SourceAsset, OuterForOwnedObjects);
	}
	else
	{
		DNAAsset = nullptr;
	}
}

TArray<FCustomizableObjectPinType> URigLogicMutableExtension::GetPinTypes() const
{
	TArray<FCustomizableObjectPinType> Result;
	
	FCustomizableObjectPinType& DNAType = Result.AddDefaulted_GetRef();
	DNAType.Name = DNAPinType;
	DNAType.DisplayName = FText::FromString(TEXT("RigLogic DNA"));
	DNAType.Color = FLinearColor::Red;

	return Result;
}

TArray<FObjectNodeInputPin> URigLogicMutableExtension::GetAdditionalObjectNodePins() const
{
	TArray<FObjectNodeInputPin> Result;

	FObjectNodeInputPin& DNAInputPin = Result.AddDefaulted_GetRef();
	DNAInputPin.PinType = DNAPinType;
	DNAInputPin.PinName = DNABaseNodePinName;
	DNAInputPin.DisplayName = FText::FromString(TEXT("RigLogic DNA"));
	DNAInputPin.bIsArray = false;

	return Result;
}

void URigLogicMutableExtension::OnSkeletalMeshCreated(
	const TArray<FInputPinDataContainer>& InputPinData,
	int32 ComponentIndex,
	USkeletalMesh* SkeletalMesh) const
{
	// Find the DNA produced by the Customizable Object, if any, and assign it to the Skeletal Mesh

	for (const FInputPinDataContainer& Container : InputPinData)
	{
		if (Container.Pin.PinName == DNABaseNodePinName)
		{
			const FDNAPinData* Data = Container.Data.GetPtr<FDNAPinData>();
			if (Data
				&& Data->ComponentIndex == ComponentIndex
				&& Data->GetDNAAsset() != nullptr)
			{
				UDNAAsset* NewDNA = CopyDNAAsset(Data->GetDNAAsset(), SkeletalMesh);
				SkeletalMesh->AddAssetUserData(NewDNA);

				// A mesh can only have one DNA at a time, so if the CO produced multiple DNA
				// Assets, all but the first will be discarded.
				break;
			}
		}
	}
}

UDNAAsset* URigLogicMutableExtension::CopyDNAAsset(const UDNAAsset* Source, UObject* OuterForCopy)
{
	check(IsInGameThread());

	// Currently the only way to copy a UDNAAsset is to serialize it into a buffer and deserialize
	// the buffer into the copy.

	// Serialize the existing DNA into a buffer
	TArray<uint8> Buffer;
	{
		FMemoryWriter Writer(Buffer);

		// Need to const_cast because Serialize is non-const. Source will not be modified.
		const_cast<UDNAAsset*>(Source)->Serialize(Writer);

		Writer.Close();
	}

	// Create the new DNA asset and deserialize the buffer into it
	UDNAAsset* Result = NewObject<UDNAAsset>(OuterForCopy);
	{
		FMemoryReader Reader(Buffer);

		Result->Serialize(Reader);
	}

	return Result;
}
