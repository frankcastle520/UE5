// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuCOE/Nodes/CustomizableObjectNodeComponent.h"
#include "MuCOE/RemapPins/CustomizableObjectNodeRemapPinsByNameDefaultPin.h"
#include "SGraphNode.h"
#include "GameplayTagContainer.h"

#include "CustomizableObjectNodeComponentPassthroughMesh.generated.h"

namespace ENodeTitleType { enum Type : int; }

class FArchive;
class FAssetThumbnail;
class FAssetThumbnailPool;
class FSkeletalMeshModel;
class ISinglePropertyView;
class UAnimInstance;
class UCustomizableObjectLayout;
class UCustomizableObjectNodeRemapPins;
class UCustomizableObjectNodeRemapPinsByName;
class UMaterialInterface;
class UObject;
class USkeletalMesh;
class UTexture2D;
struct FPropertyChangedEvent;
struct FSkeletalMaterial;


/** PinData of a pin that belongs to a Skeletal Mesh Section. */
UCLASS()
class UCustomizableObjectNodeComponentMeshPinDataSection : public UCustomizableObjectNodePinData
{
	GENERATED_BODY()

public:
	void Init(int32 InLODIndex, int32 InSectionIndex);

	int32 GetLODIndex() const;

	int32 GetSectionIndex() const;

protected:
	virtual bool Equals(const UCustomizableObjectNodePinData& Other) const override;

private:
	UPROPERTY()
	int32 LODIndex = -1;

	UPROPERTY()
	int32 SectionIndex = -1;
};


/** PinData of a Mesh pin. */
UCLASS()
class UCustomizableObjectNodeComponentMeshPinDataMaterial : public UCustomizableObjectNodeComponentMeshPinDataSection
{
	GENERATED_BODY()
};


UCLASS()
class CUSTOMIZABLEOBJECTEDITOR_API UCustomizableObjectNodeComponentPassthroughMesh : public UCustomizableObjectNodeComponent
{
public:
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = PassthroughMesh)
	FName ComponentName;

	UPROPERTY(EditAnywhere, Category = PassthroughMesh, Meta = (DisplayName = Mesh, AllowedClasses = "/Script/Engine.StaticMesh, /Script/Engine.SkeletalMesh"))
	FSoftObjectPath Mesh;

	// UObject interface.
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	
	// UEdGraphNode interface
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FLinearColor GetNodeTitleColor() const override;
	virtual FText GetTooltipText() const override;

	// UCustomizableObjectNode interface
	virtual void AllocateDefaultPins(UCustomizableObjectNodeRemapPins* RemapPins) override;
	virtual bool IsExperimental() const override;

	// Own interface
	
	void GetPinSection(const UEdGraphPin& Pin, int32& OutLODIndex, int32& OutSectionIndex) const;

	/** Find the pin for a given lod and section. */
	UEdGraphPin* GetMaterialPin(const int32 LODIndex, const int32 SectionIndex) const;

	/** Returns the material associated to the given output pin. */
	UMaterialInterface* GetMaterialFor(const UEdGraphPin* Pin) const;
	FSkeletalMaterial* GetSkeletalMaterialFor(const UEdGraphPin& Pin) const;

private:
	UMaterialInterface* GetMaterialInterfaceFor(const int32 LODIndex, const int32 MaterialIndex, const FSkeletalMeshModel* ImportedModel = nullptr) const;
	FSkeletalMaterial* GetSkeletalMaterialFor(const int32 LODIndex, const int32 MaterialIndex, const FSkeletalMeshModel* ImportedModel = nullptr) const;

	static const FName OutputPinName;

};
