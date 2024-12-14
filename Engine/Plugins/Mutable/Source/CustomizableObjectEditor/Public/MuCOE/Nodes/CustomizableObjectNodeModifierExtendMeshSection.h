// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuCOE/Nodes/CustomizableObjectNodeModifierWithMaterial.h"
#include "MuCOE/CustomizableObjectLayout.h"

#include "CustomizableObjectNodeModifierExtendMeshSection.generated.h"

namespace ENodeTitleType { enum Type : int; }

class FArchive;
class UCustomizableObject;
class UCustomizableObjectNode;
class UCustomizableObjectNodeRemapPins;
class UEdGraphPin;
class UObject;
struct FCustomizableObjectNodeExtendMaterialImage;
struct FEdGraphPinReference;


UCLASS()
class CUSTOMIZABLEOBJECTEDITOR_API UCustomizableObjectNodeModifierExtendMeshSection : public UCustomizableObjectNodeModifierWithMaterial
{
	GENERATED_BODY()

public:

	/** Tags enabled when the modifier is applied. Also used to decide what modifiers get applied to the data added by this node.  */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = EnableTags)
	TArray<FString> Tags;

public:
	
	// Begin EdGraphNode interface
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FText GetTooltipText() const override;

	// UCustomizableObjectNode interface
	virtual void BackwardsCompatibleFixup(int32 CustomizableObjectCustomVersion) override;
	virtual void AllocateDefaultPins(UCustomizableObjectNodeRemapPins* RemapPins) override;
	virtual FString GetRefreshMessage() const override;
	virtual bool IsSingleOutputNode() const override;
	virtual bool CustomRemovePin(UEdGraphPin& Pin) override;
	virtual TArray<FString>* GetEnableTags() override;
	virtual FString GetInternalTagDisplayName() override;

	// Own interface
	UEdGraphPin* AddMeshPin() const;

	TArray<UCustomizableObjectLayout*> GetLayouts() const;

private:

	// Deprecated properties

	UPROPERTY()
	TObjectPtr<UCustomizableObject> ParentMaterialObject_DEPRECATED = nullptr;

	UPROPERTY()
	FGuid ParentMaterialNodeId_DEPRECATED;
	

	UPROPERTY()
	TMap<FNodeMaterialParameterId, FEdGraphPinReference> PinsParameterMap_DEPRECATED;

	UPROPERTY()
	TMap<FGuid, FEdGraphPinReference> PinsParameter_DEPRECATED;

	UPROPERTY()
	TArray<FCustomizableObjectNodeExtendMaterialImage> Images_DEPRECATED;
};

