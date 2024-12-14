// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "ChaosFlesh/ChaosDeformableSolverGroups.h"
#include "ChaosFlesh/FleshCollection.h"
#include "Dataflow/DataflowNodeParameters.h"
#include "Dataflow/DataflowEngineTypes.h"
#include "Dataflow/DataflowContent.h"
#include "Engine/StaticMesh.h"
#include "UObject/ObjectMacros.h"

#include "FleshAsset.generated.h"

class UFleshAsset;
class UDataflow;
class USkeletalMesh;
class USkeleton;

/**
*	FFleshAssetEdit
*     Structured RestCollection access where the scope
*     of the object controls serialization back into the
*     dynamic collection
*
*/
class CHAOSFLESHENGINE_API FFleshAssetEdit
{
public:
	typedef TFunctionRef<void()> FPostEditFunctionCallback;
	friend UFleshAsset;

	/**
	 * @param UFleshAsset				The FAsset to edit
	 */
	FFleshAssetEdit(UFleshAsset* InAsset, FPostEditFunctionCallback InCallable);
	~FFleshAssetEdit();

	FFleshCollection* GetFleshCollection();

private:
	FPostEditFunctionCallback PostEditCallback;
	UFleshAsset* Asset;
};

/**
* UFleshAsset (UObject)
*
* UObject wrapper for the FFleshAsset
*
*/
UCLASS(BlueprintType, customconstructor)
class CHAOSFLESHENGINE_API UFleshAsset : public UObject, public IDataflowContentOwner
{
	GENERATED_UCLASS_BODY()
	friend class FFleshAssetEdit;

	//
	// FleshCollection
	// 
	// The FleshCollection stores all the user per-particle properties 
	// for the asset. This is used for simulation and artists 
	// configuration. Only edit the FleshCollection using its Edit
	// object. For example;
	// 
	// {
	//		FFleshAssetEdit EditObject = UFleshAsset->EditCollection();
	//		if( TSharedPtr<FFleshCollection> FleshCollection = EditObject.GetFleshCollection() )
	//      {
	//		}
	//		// the destructor of the edit object will perform invalidation. 
	// }
	//
	TSharedPtr<FFleshCollection, ESPMode::ThreadSafe> FleshCollection;

	void PostEditCallback();

public:

	UFleshAsset(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());


#if WITH_EDITOR
	/** Post edit change property */
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif //if WITH_EDITOR


	/**Editing the collection should only be through the edit object.*/
	void SetCollection(FFleshCollection* InCollection);
	const FFleshCollection* GetCollection() const { return FleshCollection.Get(); }
	FFleshCollection* GetCollection() { return FleshCollection.Get(); }

	TManagedArray<FVector3f>& GetPositions();
	const TManagedArray<FVector3f>* FindPositions() const;

	FFleshAssetEdit EditCollection() const {
		UFleshAsset* ThisNC = const_cast<UFleshAsset*>(this); 
		return FFleshAssetEdit(ThisNC, [ThisNC]() {ThisNC->PostEditCallback(); });
	}

	void Serialize(FArchive& Ar);

	//~ Begin IDataflowContentOwner interface
	virtual TObjectPtr<UDataflowBaseContent> CreateDataflowContent() override;
	virtual void WriteDataflowContent(const TObjectPtr<UDataflowBaseContent>& DataflowContent) const override;
	virtual void ReadDataflowContent(const TObjectPtr<UDataflowBaseContent>& DataflowContent) override;
	//~ End IDataflowContentOwner interface
	
	//
	// Dataflow
	//
	UPROPERTY(EditAnywhere, Category = "Dataflow")
	TObjectPtr<UDataflow> DataflowAsset;

	UPROPERTY(EditAnywhere, Category = "Dataflow")
	FString DataflowTerminal = "FleshAssetTerminal";

	UPROPERTY(EditAnywhere, Category = "Dataflow")
	TArray<FStringValuePair> Overrides;

	//
	// SkeletalMesh
	//
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation")
	TObjectPtr<USkeletalMesh> SkeletalMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation")
	TObjectPtr<USkeleton> Skeleton;

	/**
	* Skeleton to use with the flesh deformer or \c GetSkeletalMeshEmbeddedPositions() on the flesh component. 
	* Bindings for this skeletal mesh must be stored in the rest collection.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rendering")
	TObjectPtr<USkeletalMesh> TargetDeformationSkeleton;

	//
	// SkeletalMesh
	//
	UPROPERTY(EditAnywhere, Category = "Geometry")
	TObjectPtr<UStaticMesh> StaticMesh;

#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, Category = "Render")
	bool bRenderInEditor = true;

	/** Information for thumbnail rendering */
	UPROPERTY()
	TObjectPtr<class UThumbnailInfo> ThumbnailInfo;

	/*
	* The following PreviewScene properties are modeled after PreviewSkeletalMesh in USkeleton
	*	- they are inside WITH_EDITORONLY_DATA because they are not used at game runtime
	*	- TSoftObjectPtrs since that will make it possible to avoid loading these assets until the PreviewScene asks for them
	*	- DuplicateTransient so that if you copy a ClothAsset it won't copy these preview properties
	*	- AssetRegistrySearchable makes it so that if the user searches the name of a PreviewScene asset in the Asset Browser
	*/

	/** Animation asset used in this asset */
	UPROPERTY(DuplicateTransient, AssetRegistrySearchable)
	TSoftObjectPtr<UAnimationAsset> PreviewAnimationAsset = nullptr;

	UPROPERTY(DuplicateTransient, AssetRegistrySearchable)
	FSolverTimingGroup PreviewSolverTiming;

	UPROPERTY(DuplicateTransient, AssetRegistrySearchable)
	FSolverEvolutionGroup PreviewSolverEvolution;

	UPROPERTY(DuplicateTransient, AssetRegistrySearchable)
	FSolverCollisionsGroup PreviewSolverCollisions;

	UPROPERTY(DuplicateTransient, AssetRegistrySearchable)
	FSolverConstraintsGroup PreviewSolverConstraints;

	UPROPERTY(DuplicateTransient, AssetRegistrySearchable)
	FSolverForcesGroup PreviewSolverForces;

	UPROPERTY(DuplicateTransient, AssetRegistrySearchable)
	FSolverDebuggingGroup PreviewSolverDebugging;

	UPROPERTY(DuplicateTransient, AssetRegistrySearchable)
	FSolverMuscleActivationGroup PreviewSolverMuscleActivation;
	
#endif // WITH_EDITORONLY_DATA

};


/** 
 * Dataflow content owning dataflow and solver properties that will be used to evaluate the graph
 */
UCLASS()
class CHAOSFLESHENGINE_API  UDataflowFleshContent : public UDataflowSkeletalContent
{
	GENERATED_BODY()

public:
	UDataflowFleshContent();
	virtual ~UDataflowFleshContent() override {}

	//~ UObject interface
	static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);

	/** Set all the preview actor exposed properties */
	virtual void SetActorProperties(TObjectPtr<AActor>& PreviewActor) const override;

	UPROPERTY(EditAnywhere, Category = "Preview", Transient, SkipSerialization)
	FSolverTimingGroup SolverTiming;

	UPROPERTY(EditAnywhere, Category = "Preview", Transient, SkipSerialization)
	FSolverEvolutionGroup SolverEvolution;

	UPROPERTY(EditAnywhere, Category = "Preview", Transient, SkipSerialization)
	FSolverCollisionsGroup SolverCollisions;

	UPROPERTY(EditAnywhere, Category = "Preview", Transient, SkipSerialization)
	FSolverConstraintsGroup SolverConstraints;

	UPROPERTY(EditAnywhere, Category = "Preview", Transient, SkipSerialization)
	FSolverForcesGroup SolverForces;

	UPROPERTY(EditAnywhere, Category = "Preview", Transient, SkipSerialization)
	FSolverDebuggingGroup SolverDebugging;

	UPROPERTY(EditAnywhere, Category = "Preview", Transient, SkipSerialization)
	FSolverMuscleActivationGroup SolverMuscleActivation;
};

