// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/StaticMeshComponent.h"
#include "ChaosVDGeometryDataComponent.h"
#include "Interfaces/ChaosVDPooledObject.h"
#include "ChaosVDStaticMeshComponent.generated.h"

/** CVD version of a Static Mesh Component that holds additional CVD data */
UCLASS(HideCategories=("Transform"), MinimalAPI)
class UChaosVDStaticMeshComponent : public UStaticMeshComponent, public IChaosVDGeometryComponent, public IChaosVDPooledObject
{
	GENERATED_BODY()

public:
	UChaosVDStaticMeshComponent(const FObjectInitializer& ObjectInitializer)
		: Super(ObjectInitializer)
	{
		SetCanEverAffectNavigation(false);
		bNavigationRelevant = false;
		bOverrideWireframeColor = true;
		WireframeColorOverride = FColor::White;
	}

	// BEGIN IChaosVDGeometryDataComponent Interface

	virtual bool IsMeshReady() const override { return bIsMeshReady; }
	
	virtual void SetIsMeshReady(bool bIsReady) override { bIsMeshReady = bIsReady; }

	virtual FChaosVDMeshReadyDelegate* OnMeshReady() override { return &MeshReadyDelegate; }
	
	virtual FChaosVDMeshComponentEmptyDelegate* OnComponentEmpty() override { return &ComponentEmptyDelegate; }

	virtual uint32 GetGeometryKey() const override;
	virtual void UpdateInstanceVisibility(const TSharedPtr<FChaosVDMeshDataInstanceHandle>& InInstanceHandle, bool bIsVisible) override;

	virtual void SetIsSelected(const TSharedPtr<FChaosVDMeshDataInstanceHandle>& InInstanceHandle, bool bIsSelected) override;

	virtual bool ShouldRenderSelected() const override;

	virtual void UpdateInstanceColor(const TSharedPtr<FChaosVDMeshDataInstanceHandle>& InInstanceHandle, FLinearColor NewColor) override;
	virtual void UpdateInstanceWorldTransform(const TSharedPtr<FChaosVDMeshDataInstanceHandle>& InInstanceHandle, const FTransform& InTransform) override;

	virtual void SetMeshComponentAttributeFlags(EChaosVDMeshAttributesFlags Flags) override { MeshComponentAttributeFlags = Flags; };
	virtual EChaosVDMeshAttributesFlags GetMeshComponentAttributeFlags() const override { return MeshComponentAttributeFlags; };

	virtual TSharedPtr<FChaosVDMeshDataInstanceHandle> GetMeshDataInstanceHandle(int32 InstanceIndex) const override;
	virtual TArrayView<TSharedPtr<FChaosVDMeshDataInstanceHandle>> GetMeshDataInstanceHandles() override;

	virtual void Initialize() override;
	virtual void Reset() override;

	virtual TSharedPtr<FChaosVDMeshDataInstanceHandle> AddMeshInstance(const FTransform InstanceTransform, bool bIsWorldSpace, const TSharedPtr<FChaosVDExtractedGeometryDataHandle>& InGeometryHandle, int32 ParticleID, int32 SolverID) override;
	virtual void AddMeshInstanceForHandle(TSharedPtr<FChaosVDMeshDataInstanceHandle> MeshDataHandle, const FTransform InstanceTransform, bool bIsWorldSpace, const TSharedPtr<FChaosVDExtractedGeometryDataHandle>& InGeometryHandle, int32 ParticleID, int32 SolverID) override;
	virtual void RemoveMeshInstance(TSharedPtr<FChaosVDMeshDataInstanceHandle> InHandleToRemove) override;

	virtual void SetGeometryBuilder(TWeakPtr<FChaosVDGeometryBuilder> GeometryBuilder) override;

	virtual EChaosVDMaterialType GetMaterialType() const override;

	// END IChaosVDGeometryDataComponent Interface

	// BEGIN IChaosVDPooledObject Interface
	virtual void OnAcquired() override {};
	virtual void OnDisposed() override;
	// END IChaosVDPooledObject Interface

protected:

	bool UpdateGeometryKey(uint32 NewHandleGeometryKey);

	EChaosVDMeshAttributesFlags MeshComponentAttributeFlags = EChaosVDMeshAttributesFlags::None;
	uint8 CurrentGeometryKey = 0;
	bool bIsMeshReady = false;
	bool bIsOwningParticleSelected = false;
	FChaosVDMeshReadyDelegate MeshReadyDelegate;
	FChaosVDMeshComponentEmptyDelegate ComponentEmptyDelegate;

	TSharedPtr<FChaosVDMeshDataInstanceHandle> CurrentMeshDataHandle = nullptr;

	TSharedPtr<FChaosVDExtractedGeometryDataHandle> CurrentGeometryHandle = nullptr;
	
	TWeakPtr<FChaosVDGeometryBuilder> GeometryBuilderWeakPtr;
};
