// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CustomizableObjectNodeComponentMesh.h"
#include "CustomizableObjectNodeComponentMeshBase.h"
#include "MuCOE/Nodes/CustomizableObjectNode.h"
#include "MuR/System.h"

#include "CustomizableObjectNodeObject.generated.h"

namespace ENodeTitleType { enum Type : int; }

class UCustomizableObject;
class UCustomizableObjectNodeMaterialBase;
class UCustomizableObjectNodeRemapPins;
class UObject;
struct FPropertyChangedEvent;
struct FSoftObjectPath;
struct FComponentSettings;


USTRUCT()
struct CUSTOMIZABLEOBJECTEDITOR_API FCustomizableObjectState
{
	GENERATED_USTRUCT_BODY()

	friend UCustomizableObjectNodeObject;
	
	UPROPERTY(EditAnywhere, Category=CustomizableObject)
	FString Name;

	UPROPERTY(EditAnywhere, Category = CustomizableObject, meta = (ShowParameterOptions))
	TArray<FString> RuntimeParameters;

	/** Special treatment of texture compression for this state. */
	UPROPERTY(EditAnywhere, Category = CustomizableObject)
	ETextureCompressionStrategy TextureCompressionStrategy = ETextureCompressionStrategy::None;

	/** If this is enabled, texture streaming won't be used for this state, and full images will be generated when an instance is first updated. */
	UPROPERTY(EditAnywhere, Category = CustomizableObject)
	bool bDisableTextureStreaming = false;

	/** LiveUpdateMode will reuse instance temp. data between updates and speed up update times, but spend much more memory. Good for customization screens, not for actual gameplay modes. */
	UPROPERTY(EditAnywhere, Category = CustomizableObject)
	bool bLiveUpdateMode = false;

	// Enables the reuse of all possible textures when the instance is updated without any changes in geometry or state (the first update after creation doesn't reuse any)
	// It will only work if the textures aren't compressed, so set the instance to a Mutable state with texture compression disabled
	// WARNING! If texture reuse is enabled, do NOT keep external references to the textures of the instance. The instance owns the textures.
	UPROPERTY(EditAnywhere, Category = CustomizableObject)
	bool bReuseInstanceTextures = false;

	UPROPERTY(EditAnywhere, Category = CustomizableObject)
	bool bBuildOnlyFirstLOD = false;

	/** If there's an entry for a specific platform, when compiling for that platform Num LODs will be built after bBuildOnlyFirstLOD */
	UPROPERTY(EditAnywhere, Category = CustomizableObject)
	TMap<FString, int32> NumExtraLODsToBuildPerPlatform;

	UPROPERTY(EditAnywhere, Category = CustomizableObject)
	TMap<FString, FString> ForcedParameterValues;

	UPROPERTY(EditAnywhere, Category = UI, meta = (DisplayName = "State UI Metadata"))
	FMutableStateUIMetadata UIMetadata;

private:
	// Deprecated
	UPROPERTY()
	FMutableParamUIMetadata StateUIMetadata_DEPRECATED;
	
	/** This is now TextureCompressionStrategy.  */
	UPROPERTY()
	bool bDontCompressRuntimeTextures_DEPRECATED = false;
};

USTRUCT()
struct FSkeletalMeshMorphTargetOverride
{
	GENERATED_BODY()
	
	UPROPERTY(EditAnywhere, Category = RealTimeMorphTargets)
	FName SkeletalMeshName;

	UPROPERTY(EditAnywhere, Category = RealTimeMorphTargets)
	ECustomizableObjectSelectionOverride SelectionOverride = ECustomizableObjectSelectionOverride::NoOverride;
};

USTRUCT()
struct FRealTimeMorphSelectionOverride
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = RealTimeMorphTargets)
	FName MorphName;

	UPROPERTY(EditAnywhere, Category = RealTimeMorphTargets)
	ECustomizableObjectSelectionOverride SelectionOverride = ECustomizableObjectSelectionOverride::NoOverride;

	UPROPERTY(EditAnywhere, Category = RealTimeMorphTargets, meta=(TitleProperty=SkeletalMeshName))
	TArray<FSkeletalMeshMorphTargetOverride> SkeletalMeshes;

    UPROPERTY()
    TArray<FName> SkeletalMeshesNames_DEPRECATED;

    UPROPERTY()
    TArray<ECustomizableObjectSelectionOverride> Override_DEPRECATED;
};

UCLASS()
class CUSTOMIZABLEOBJECTEDITOR_API UCustomizableObjectNodeObject : public UCustomizableObjectNode
{
public:
	GENERATED_BODY()

	UCustomizableObjectNodeObject();

	UPROPERTY(EditAnywhere, Category=CustomizableObject)
	FString ObjectName;

	UPROPERTY(EditAnywhere, Category = UI, meta = (DisplayName = "Parameter UI Metadata"))
	FMutableParamUIMetadata ParamUIMetadata;

	UPROPERTY()
	int32 NumLODs_DEPRECATED = 1;

	UPROPERTY()
	ECustomizableObjectAutomaticLODStrategy AutoLODStrategy_DEPRECATED = ECustomizableObjectAutomaticLODStrategy::AutomaticFromMesh;

	UPROPERTY()
	int32 NumMeshComponents_DEPRECATED = 1;

	UPROPERTY(EditAnywhere, Category=CustomizableObject ,meta = (TitleProperty = "Name"))
	TArray<FCustomizableObjectState> States;

	UPROPERTY(EditAnywhere, Category = AttachedToExternalObject)
	TObjectPtr<UCustomizableObject> ParentObject;

	UPROPERTY(EditAnywhere, Category = AttachedToExternalObject)
	FGuid ParentObjectGroupId;

	UPROPERTY()
	FGuid Identifier;

	UPROPERTY(EditAnywhere, Category = RealTimeMorphTargets, meta=(TitleProperty=MorphName, EditCondition="ParentObject==nullptr"))
	TArray<FRealTimeMorphSelectionOverride> RealTimeMorphSelectionOverrides;

	UPROPERTY()
	TArray<FComponentSettings> ComponentSettings_DEPRECATED;
		
	// UObject interface.
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;

	// EdGraphNode interface
	FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	FLinearColor GetNodeTitleColor() const override;
	FText GetTooltipText() const override;
	void PrepareForCopying() override;
	void PostPasteNode() override;
	void PostDuplicate(bool bDuplicateForPIE) override;

	// UCustomizableObjectNode interface
	virtual void BackwardsCompatibleFixup(int32 CustomizableObjectCustomVersion) override;
	virtual void PostBackwardsCompatibleFixup() override;
	void AllocateDefaultPins(UCustomizableObjectNodeRemapPins* RemapPins) override;

	// Own Interface
	void SetParentObject(UCustomizableObject* CustomizableParentObject);

	// Own interface
	UPROPERTY()
	bool bIsBase;
	
	UEdGraphPin* ComponentsPin() const
	{
		return FindPin(ComponentsPinName);
	}

	UEdGraphPin* ModifiersPin() const
	{
		return FindPin(ModifiersPinName);
	}

	UEdGraphPin* ChildrenPin() const
	{
		return FindPin(ChildrenPinName);
	}

	UEdGraphPin* OutputPin() const
	{
		return FindPin(OutputPinName);
	}

	
	virtual bool CanUserDeleteNode() const override;
	virtual bool CanDuplicateNode() const override;
	
	bool IsSingleOutputNode() const override;
	
	// Array filled in the Details of the node to store all the parameter names of a CO graph (full tree)
	TArray<FString> ParameterNames;

private:
	static const FName ChildrenPinName;
	static const FName ComponentsPinName;
	static const FName ModifiersPinName;
	static const FName OutputPinName;
};

