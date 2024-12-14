// Copyright Epic Games, Inc. All Rights Reserved.

#include "BaseGizmos/TransformSubGizmoUtil.h"

#include "BaseGizmos/CombinedTransformGizmo.h"
#include "BaseGizmos/GizmoRenderingUtil.h"
#include "BaseGizmos/GizmoPrivateUtil.h" // ToAxis, GetGizmoViewContext
#include "BaseGizmos/ViewBasedTransformAdjusters.h"
#include "BaseGizmos/ViewAdjustedStaticMeshGizmoComponent.h"
#include "Components/SceneComponent.h"
#include "Misc/EnumRange.h" // MakeFlagsRange


FTransform UE::GizmoUtil::GetRotatedBasisTransform(const FTransform& TransformIn, EAxis::Type AxisToBeX)
{
	if (AxisToBeX == EAxis::Y)
	{
		FQuat RotationQuat(FMatrix(FVector::YAxisVector, FVector::ZAxisVector, FVector::XAxisVector, FVector::ZeroVector));
		return TransformIn * RotationQuat;
	}
	else if (AxisToBeX == EAxis::Z)
	{
		FQuat RotationQuat(FMatrix(FVector::ZAxisVector, FVector::XAxisVector, FVector::YAxisVector, FVector::ZeroVector));
		return TransformIn * RotationQuat;
	}

	return TransformIn;
}

void UE::GizmoUtil::InitializeSubGizmoElementsWithMesh(const FInitMeshSubGizmoParams& Params,
	TArray<TPair<ETransformGizmoSubElements, UViewAdjustedStaticMeshGizmoComponent*>>* ComponentsOut)
{
	if (!ensure(Params.ParentGizmo && Params.Mesh))
	{
		return;
	}

	ACombinedTransformGizmoActor* GizmoActor = Params.ParentGizmo->GetGizmoActor();
	UGizmoViewContext* GizmoViewContext = GetGizmoViewContext(Params.ParentGizmo->GetGizmoManager());

	if (!ensure(GizmoActor && GizmoViewContext))
	{
		return;
	}

	// Iterate over each element individually
	for (ETransformGizmoSubElements Element : MakeFlagsRange(Params.Elements))
	{
		EAxis::Type ElementAxis = ToAxis(Element);
		FLinearColor Color = ElementAxis == EAxis::None ? FLinearColor::Black 
			: UE::GizmoRenderingUtil::GetDefaultAxisColor(ElementAxis);

		UViewAdjustedStaticMeshGizmoComponent* Component = UE::GizmoRenderingUtil::CreateDefaultMaterialGizmoMeshComponent(
			Params.Mesh, GizmoViewContext, GizmoActor, Color);

		// This makes it so that the component takes the place of the proper element
		Params.ParentGizmo->SetSubGizmoComponent(Element, Component, FTransform::Identity);

		// This has to happen after the SetSubGizmoComponent call above, so that it is already attached
		Component->SetRelativeTransform(
			Params.bRotateTransformBasisBasedOnElement ? GetRotatedBasisTransform(Params.ComponentToGizmo, ElementAxis)
			: Params.ComponentToGizmo);

		UE::GizmoRenderingUtil::FSubGizmoTransformAdjuster::AddTransformAdjuster(
			Component, GizmoActor->GetRootComponent(), Params.bMirrorBasedOnOctant);

		if (Params.SubstituteInteractionMesh)
		{
			UViewAdjustedStaticMeshGizmoComponent* SubstituteComponent = nullptr;
			
			SubstituteComponent = UE::GizmoRenderingUtil::CreateDefaultMaterialGizmoMeshComponent(
				Params.SubstituteInteractionMesh, GizmoViewContext, Component, Color, 
				// No need for hover material
				false);

			Component->SetSubstituteInteractionComponent(SubstituteComponent, Params.SubstituteMeshToComponent);

			SubstituteComponent->SetRelativeTransform(Params.SubstituteMeshToComponent);
			UE::GizmoRenderingUtil::FSubGizmoTransformAdjuster::AddTransformAdjuster(
				SubstituteComponent, GizmoActor->GetRootComponent(), Params.bMirrorBasedOnOctant);
		}//end substitute interaction mesh setup

		if (ComponentsOut)
		{
			ComponentsOut->Emplace(Element, Component);
		}
	}//end for each element
}