// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Data/PCGSpatialData.h"

#include "DynamicMesh/DynamicMeshOctree3.h"
#include "DynamicMesh/MeshIndexMappings.h"

#include "Misc/SpinLock.h"

#include "PCGDynamicMeshData.generated.h"

struct FPCGContext;
class UDynamicMesh;
class UDynamicMeshComponent;
class UMaterialInterface;

UCLASS(BlueprintType, ClassGroup = (Procedural))
class PCGGEOMETRYSCRIPTINTEROP_API UPCGDynamicMeshData : public UPCGSpatialData
{
	GENERATED_BODY()

public:
	UPCGDynamicMeshData(const FObjectInitializer& ObjectInitializer);
	
	void Initialize(UDynamicMesh* InMesh, bool bCanTakeOwnership = false, const TArray<UMaterialInterface*>& InOptionalMaterials = {});
	void Initialize(UE::Geometry::FDynamicMesh3&& InMesh, const TArray<UMaterialInterface*>& InOptionalMaterials = {});

	/**
	 * Initialize the dynamic mesh data from an input dynamic mesh object.
	 * If the input dynamic mesh is not meant to be re-used after this initialization, you can set Can Take Ownership to true. Be careful as it
	 * will put the previous object in an invalid state.
	 * You can also pass an array of materials that correspond to the referenced materials in the dynamic mesh.
	 */
	UFUNCTION(BlueprintCallable, Category="DynamicMesh", meta = (DisplayName = "Initialize", AutoCreateRefTerm = "InMaterials"))
	void K2_Initialize(UDynamicMesh* InMesh, const TArray<UMaterialInterface*>& InMaterials, bool bCanTakeOwnership = false) { Initialize(InMesh, bCanTakeOwnership, InMaterials); }
	
	// ~Begin UPCGData interface
	virtual EPCGDataType GetDataType() const override { return EPCGDataType::DynamicMesh; }
	virtual void AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const override;
	// ~End UPCGData interface

	// ~Begin UPCGSpatialData interface
	virtual int GetDimension() const override { return 3; }
	virtual FBox GetBounds() const override;
	virtual bool SamplePoint(const FTransform& Transform, const FBox& Bounds, FPCGPoint& OutPoint, UPCGMetadata* OutMetadata) const override;
	// TODO needs an implementation to support projection
	//virtual bool ProjectPoint(const FTransform& InTransform, const FBox& InBounds, const FPCGProjectionParams& InParams, FPCGPoint& OutPoint, UPCGMetadata* OutMetadata) const;
	//~End UPCGSpatialData interface

	const UE::Geometry::FDynamicMeshOctree3& GetDynamicMeshOctree() const;
	
	UDynamicMesh* GetMutableDynamicMesh() { bDynamicMeshBoundsAreDirty = true; bDynamicMeshOctreeIsDirty = true; return DynamicMesh; }
	const UDynamicMesh* GetDynamicMesh() const { return DynamicMesh; }

	UFUNCTION(BlueprintCallable, Category="DynamicMesh")
	void SetMaterials(const TArray<UMaterialInterface*>& InMaterials);
	
	TArray<TObjectPtr<UMaterialInterface>>& GetMutableMaterials() { return Materials; }
	const TArray<TObjectPtr<UMaterialInterface>>& GetMaterials() const { return Materials; }

	// Copy the mesh of the data into the component and set the materials.
	void InitializeDynamicMeshComponentFromData(UDynamicMeshComponent* InComponent) const;

protected:
	// ~Begin UPCGSpatialData interface
	virtual UPCGSpatialData* CopyInternal(FPCGContext* Context) const override;
public:
	virtual const UPCGPointData* ToPointData(FPCGContext* Context, const FBox& InBounds) const override;
	//~End UPCGSpatialData interface

private:
	// const but will set the mutable CachedBounds
	void ResetBounds() const;

protected:
	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Instanced, Category="DynamicMesh")
	TObjectPtr<UDynamicMesh> DynamicMesh;
	
	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Instanced, Category="DynamicMesh")
	TArray<TObjectPtr<UMaterialInterface>> Materials;

	mutable UE::Geometry::FDynamicMeshOctree3 DynamicMeshOctree;
	mutable bool bDynamicMeshOctreeIsDirty = true;
	mutable FCriticalSection DynamicMeshOctreeLock;
	
	mutable FBox CachedBounds = FBox(EForceInit::ForceInit);
	mutable bool bDynamicMeshBoundsAreDirty = true;
	mutable UE::FSpinLock DynamicMeshBoundsLock;
};
