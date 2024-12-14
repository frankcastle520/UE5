// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "WorldPartition/WorldPartitionActorDesc.h"
#include "GameFramework/Actor.h"

#include "MapBuildDataActor.generated.h"

class UMapBuildDataRegistry;

UCLASS(NotPlaceable, HideCategories=(Rendering, Replication, Collision, Physics, Navigation, Networking, Input, Actor, LevelInstance, Cooking))
class ENGINE_API AMapBuildDataActor : public AActor
{
	GENERATED_UCLASS_BODY()

public:
	
	void SetCellPackage(FName InCellPackage) { CellPackage = InCellPackage; }
	void SetBounds(FBox& Bounds);

#if WITH_EDITOR
	void SetActorInstances(TArray<FGuid>& InActorInstances) { ActorInstances = InActorInstances; }
#endif

	UMapBuildDataRegistry* GetBuildData(bool bCreateIfNotFound = false);
	void SetBuildData(UMapBuildDataRegistry* MapBuildData);

	void LinkToActor(AActor* Actor);

protected:
	//~ Begin UObject Interface
	virtual void PostLoad() override;
	virtual void BeginDestroy() override;
	//~ End UObject Interface

	//~ Begin AActor Interface.	
	virtual void GetActorBounds(bool bOnlyCollidingComponents, FVector& OutOrigin, FVector& OutBoxExtent, bool bIncludeFromChildActors) const override;	
	virtual void PreRegisterAllComponents() override;
	virtual void PostUnregisterAllComponents() override;
#if WITH_EDITOR
	virtual TUniquePtr<class FWorldPartitionActorDesc> CreateClassActorDesc() const override;
	virtual void PopulatePIEDuplicationSeed(FDuplicationSeedInterface& DupSeed) override;
	virtual void GetStreamingBounds(FBox& OutRuntimeBounds, FBox& OutEditorBounds) const override;
#endif
	//~ End AActor Interface.

	UPROPERTY(NonPIEDuplicateTransient)	
	TObjectPtr<UMapBuildDataRegistry> BuildData;

	UPROPERTY()
	TObjectPtr<AActor> ForceLinkToActor;

	UPROPERTY()
	FBox ActorBounds;

	UPROPERTY()
	FName CellPackage;

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	TArray<FGuid> ActorInstances;
#endif

	UPROPERTY()
	FGuid LevelBuildDataId;
	
	bool bAddedToWorld;

	void AddToWorldMapBuildData();
	void RemoveFromWorldMapBuildData();

	void InitializeRenderingResources();
	void ReleaseRenderingResources();

	friend class FMapBuildDataActorDesc;
};

#if WITH_EDITOR
class FMapBuildDataActorDesc : public FWorldPartitionActorDesc
{
	friend class AMapBuildDataActor;

	public:
		
		FName	CellPackage;

	protected:
	
		ENGINE_API FMapBuildDataActorDesc();

		//~ Begin FWorldPartitionActorDesc Interface.
		ENGINE_API virtual void Init(const AActor* InActor) override;
		ENGINE_API virtual bool Equals(const FWorldPartitionActorDesc* Other) const override;
		virtual uint32 GetSizeOf() const override { return sizeof(FMapBuildDataActorDesc); }
		ENGINE_API virtual void Serialize(FArchive& Ar) override;
		virtual bool IsRuntimeRelevant(const FWorldPartitionActorDescInstance* InActorDescInstance) const override;
		//~ End FWorldPartitionActorDesc Interface.
};

DEFINE_ACTORDESC_TYPE(AMapBuildDataActor, FMapBuildDataActorDesc );

#endif