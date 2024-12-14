// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"
#include "CoreMinimal.h"
#include "Dataflow/DataflowPreview.h"
#include "Dataflow/DataflowCore.h"
#include "EdGraph/EdGraph.h"
#include "Templates/Function.h"
#include "Templates/SharedPointer.h"
#include "UObject/ObjectMacros.h"
#include "UObject/ObjectPtr.h"
#include "UObject/UObjectGlobals.h"

#include "DataflowObject.generated.h"

class FArchive;
class UDataflow;
class UObject;
class UDataflowEdNode;
class UMaterial;
namespace UE::Dataflow { class FGraph; }


/**
*	FFleshAssetEdit
*     Structured RestCollection access where the scope
*     of the object controls serialization back into the
*     dynamic collection
*
*/
class FDataflowAssetEdit
{
public:
	typedef TFunctionRef<void()> FPostEditFunctionCallback;
	friend UDataflow;

	/**
	 * @param UDataflow				The "FAsset" to edit
	 */
	DATAFLOWENGINE_API FDataflowAssetEdit(UDataflow *InAsset, FPostEditFunctionCallback InCallable);
	DATAFLOWENGINE_API ~FDataflowAssetEdit();

	DATAFLOWENGINE_API UE::Dataflow::FGraph* GetGraph();

private:
	FPostEditFunctionCallback PostEditCallback;
	UDataflow* Asset;
};

/** Data flow types */
UENUM()
enum class EDataflowType : uint8
{
	/** the dataflow will be used to build assets */
	Construction,

	/** The dataflow will be used to define the simulation evolution */
	Simulation
};

/**
* UDataflow (UObject)
*
* UObject wrapper for the UE::Dataflow::FGraph
*
*/
UCLASS(BlueprintType, customconstructor, MinimalAPI)
class UDataflow : public UEdGraph
{
	GENERATED_UCLASS_BODY()

	UE::Dataflow::FTimestamp LastModifiedRenderTarget = UE::Dataflow::FTimestamp::Invalid; 
	TArray< TObjectPtr<const UDataflowEdNode> > RenderTargets; // Not Serialized
	TArray< TObjectPtr<const UDataflowEdNode> > WireframeRenderTargets; // Not Serialized
	TSharedPtr<UE::Dataflow::FGraph, ESPMode::ThreadSafe> Dataflow;
	DATAFLOWENGINE_API void PostEditCallback();

public:
	DATAFLOWENGINE_API UDataflow(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** 
	* Find all the node of a speific type and evaluate them using a specific UObject
	*/
	UE_DEPRECATED(5.1, "Use Blueprint library version of the function")
	DATAFLOWENGINE_API void EvaluateTerminalNodeByName(FName NodeName, UObject* Asset);

	virtual bool IsEditorOnly() const { return true; }

	/** Simulation tag to use in the node registry */
	static const inline FString SimulationTag = TEXT("DataflowSimulationTag");

public:
	UPROPERTY(EditAnywhere, Category = "Evaluation")
	bool bActive = true;

	UPROPERTY(EditAnywhere, Category = "Evaluation", AdvancedDisplay )
	TArray<TObjectPtr<UObject>> Targets;

	UPROPERTY(EditAnywhere, Category = "Render")
	TObjectPtr<UMaterial> Material = nullptr;

	UPROPERTY(EditAnywhere, Category = "Evaluation",meta=(EditConditionHides))
	EDataflowType Type = EDataflowType::Construction;

public:
	/** UObject Interface */
	static DATAFLOWENGINE_API void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);

#if WITH_EDITOR
	DATAFLOWENGINE_API virtual bool CanEditChange(const FProperty* InProperty) const override;
	DATAFLOWENGINE_API virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	DATAFLOWENGINE_API virtual void PostLoad() override;
	/** End UObject Interface */

	DATAFLOWENGINE_API void Serialize(FArchive& Ar);

	/** Accessors for internal geometry collection */
	TSharedPtr<const UE::Dataflow::FGraph, ESPMode::ThreadSafe> GetDataflow() const { return Dataflow; }
	TSharedPtr<UE::Dataflow::FGraph, ESPMode::ThreadSafe> GetDataflow() { return Dataflow; }

	/**Editing the collection should only be through the edit object.*/
	FDataflowAssetEdit EditDataflow() const {
		//ThisNC is a by-product of editing through the component.
		UDataflow* ThisNC = const_cast<UDataflow*>(this);
		return FDataflowAssetEdit(ThisNC, [ThisNC]() {ThisNC->PostEditCallback(); });
	}

	DATAFLOWENGINE_API TObjectPtr<const UDataflowEdNode> FindEdNodeByDataflowNodeGuid(const FGuid& Guid) const;
	DATAFLOWENGINE_API TObjectPtr<UDataflowEdNode> FindEdNodeByDataflowNodeGuid(const FGuid& Guid);

	//
	// Render Targets
	//
	DATAFLOWENGINE_API void AddRenderTarget(TObjectPtr<const UDataflowEdNode>);
	DATAFLOWENGINE_API void RemoveRenderTarget(TObjectPtr<const UDataflowEdNode>);
	const TArray< TObjectPtr<const UDataflowEdNode> >& GetRenderTargets() const { return RenderTargets; }

	DATAFLOWENGINE_API void AddWireframeRenderTarget(TObjectPtr<const UDataflowEdNode>);
	DATAFLOWENGINE_API void RemoveWireframeRenderTarget(TObjectPtr<const UDataflowEdNode>);
	const TArray< TObjectPtr<const UDataflowEdNode> >& GetWireframeRenderTargets() const { return WireframeRenderTargets; }

	const UE::Dataflow::FTimestamp& GetRenderingTimestamp() const { return LastModifiedRenderTarget; }

#if WITH_EDITORONLY_DATA

	/*
	* The following PreviewScene properties are modeled after PreviewSkeletalMesh in USkeleton
	*	- they are inside WITH_EDITORONLY_DATA because they are not used at game runtime
	*	- TSoftObjectPtrs since that will make it possible to avoid loading these assets until the PreviewScene asks for them
	*	- DuplicateTransient so that if you copy a ClothAsset it won't copy these preview properties
	*	- AssetRegistrySearchable makes it so that if the user searches the name of a PreviewScene asset in the Asset Browser
	*/

	/** Cachie params used in this asset */
	UPROPERTY(DuplicateTransient, AssetRegistrySearchable)
	FDataflowPreviewCacheParams PreviewCacheParams;

	/** Cache asset used in this asset */
	UPROPERTY(DuplicateTransient, AssetRegistrySearchable)
	TSoftObjectPtr<UObject> PreviewCacheAsset = nullptr;

	/** Caching blueprint actor class to spawn */
	UPROPERTY(DuplicateTransient, AssetRegistrySearchable)
	TSubclassOf<AActor> PreviewBlueprintClass = nullptr;
	
	/** Caching blueprint actor transform to spawn */
	UPROPERTY(DuplicateTransient, AssetRegistrySearchable)
	FTransform PreviewBlueprintTransform = FTransform::Identity;

#endif

#if WITH_EDITOR
	/** Used to disable per-node serialization when serializing a transaction */
	bool IsPerNodeTransactionSerializationEnabled() const { return bEnablePerNodeTransactionSerialization; }
private:

	/** Used to disable per-node serialization when serializing a transaction */
	bool bEnablePerNodeTransactionSerialization = true;
#endif
};


