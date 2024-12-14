// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CustomizableObjectNodeComponentMeshBase.h"
#include "MuR/System.h"

#include "CustomizableObjectNodeComponentMesh.generated.h"


UENUM()
enum class ECustomizableObjectSelectionOverride : uint8
{
	NoOverride = 0 UMETA(DisplayName = "No Override"),
	Disable    = 1 UMETA(DisplayName = "Disable"    ),
	Enable     = 2 UMETA(DisplayName = "Enable"     )
};


USTRUCT()
struct FBoneToRemove
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(EditAnywhere, Category = CustomizableObject)
	bool bOnlyRemoveChildren = false;

	UPROPERTY(EditAnywhere, Category = CustomizableObject)
	FName BoneName;
};


USTRUCT()
struct FLODReductionSettings
{
	GENERATED_USTRUCT_BODY()

	/** Selects which bones will be removed from the final skeleton
	* BoneName: Name of the bone that will be removed. Its children will be removed too.
	* Remove Only Children: If true, only the children of the selected bone will be removed. The selected bone will remain.
	*/
	UPROPERTY(EditAnywhere, Category = CustomizableObject)
	TArray<FBoneToRemove> BonesToRemove;
};


UCLASS()
class CUSTOMIZABLEOBJECTEDITOR_API UCustomizableObjectNodeComponentMesh : public UCustomizableObjectNodeComponentMeshBase
{
	GENERATED_BODY()

public:
	// UObject interface
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void BackwardsCompatibleFixup(int32 CustomizableObjectCustomVersion) override;

	// UEdGraphNode interface
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	
	UPROPERTY(EditAnywhere, Category = ComponentMesh)
	FName ComponentName;
	
	UPROPERTY(EditAnywhere, Category = ComponentMesh)
	TObjectPtr<USkeletalMesh> ReferenceSkeletalMesh;

	UPROPERTY(EditAnywhere, Category = ComponentMesh)
	TArray<FLODReductionSettings> LODReductionSettings;

	/** Details selected LOD. */
	int32 SelectedLOD = 0;
};
