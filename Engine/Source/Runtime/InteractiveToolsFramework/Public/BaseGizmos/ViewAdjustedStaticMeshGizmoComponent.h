// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/StaticMeshComponent.h"
#include "BaseGizmos/GizmoBaseComponent.h"

#include "ViewAdjustedStaticMeshGizmoComponent.generated.h"

class UGizmoViewContext;
namespace UE::GizmoRenderingUtil
{
	class IViewBasedTransformAdjuster;
}

/**
 * Version of a static mesh component that only takes the dynamic draw path and has the ability to
 * adjust the transform based on view information.
 */
UCLASS(Blueprintable, BlueprintType, ClassGroup = Gizmos, meta = (BlueprintSpawnableComponent))
class INTERACTIVETOOLSFRAMEWORK_API UViewAdjustedStaticMeshGizmoComponent : public UStaticMeshComponent
	, public IGizmoBaseComponentInterface
{
	GENERATED_BODY()
public:
	/**
	 * The gizmo view context is needed to be able to line trace the component, since its collision data
	 *  needs updating based on view.
	 */
	void SetGizmoViewContext(UGizmoViewContext* GizmoViewContextIn) { GizmoViewContext = GizmoViewContextIn; }
	
	void SetTransformAdjuster(TSharedPtr<UE::GizmoRenderingUtil::IViewBasedTransformAdjuster> Adjuster);
	TSharedPtr<UE::GizmoRenderingUtil::IViewBasedTransformAdjuster> GetTransformAdjuster() { return TransformAdjuster; }
	
	/** Helper method that just sets the same material in all slots. Does not include hover override material. */
	void SetAllMaterials(UMaterialInterface* Material);

	/**
	 * Sets a material that will override all material slots whenever the component is told that is being
	 *  hovered (via UpdateHoverState).
	 */
	void SetHoverOverrideMaterial(UMaterialInterface* Material);

	UMaterialInterface* GetHoverOverrideMaterial() { return HoverOverrideMaterial; }
	bool IsBeingHovered() { return bHovered; }

	/** 
	 * Sets a mesh that is swapped in while the component is being interacted with. This is done by not rendering this 
	 *  component and making the substitute component visible.
	 */
	void SetSubstituteInteractionComponent(UPrimitiveComponent* Component, const FTransform& RelativeTransform = FTransform::Identity);
	
	bool IsHiddenByInteraction() { return bInteracted && SubstituteInteractionComponent; }

	// IGizmoBaseComponentInterface
	virtual void UpdateHoverState(bool bHoveringIn) override;
	virtual void UpdateWorldLocalState(bool bWorldIn) override;
	virtual void UpdateInteractingState(bool bInteracting) override;

	// UMeshComponent
	virtual FMaterialRelevance GetMaterialRelevance(ERHIFeatureLevel::Type InFeatureLevel) const override;

	// UPrimitiveComponent
	virtual bool LineTraceComponent(FHitResult& OutHit, const FVector Start, const FVector End, const FCollisionQueryParams& Params) override;

	// UActorComponent
	virtual bool IsHLODRelevant() const override { return false; }

	// UObject
	virtual bool NeedsLoadForServer() const override { return false; }

protected:
	// UStaticMeshComponent
	virtual FPrimitiveSceneProxy* CreateStaticMeshSceneProxy(Nanite::FMaterialAudit& NaniteMaterials, bool bCreateNanite) override;

private:
		
	// Needed for proper line traces
	UPROPERTY()
	TObjectPtr<UGizmoViewContext> GizmoViewContext = nullptr;

	UPROPERTY()
	TObjectPtr<UPrimitiveComponent> SubstituteInteractionComponent = nullptr;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> HoverOverrideMaterial = nullptr;

	TSharedPtr<UE::GizmoRenderingUtil::IViewBasedTransformAdjuster> TransformAdjuster;

	bool bHovered = false;
	bool bInteracted = false;
};
